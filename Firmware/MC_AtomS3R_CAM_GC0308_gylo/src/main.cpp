#include <Wire.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_camera.h>
#include <esp_now.h>
#include <lwip/sockets.h>

// ==== BMI270 (Bosch API) ====
#include "bmi270.h"
#include "bmi2.h"

#define BMI270_ADDR 0x68

struct bmi2_dev bmi2_dev;
struct bmi2_sens_config sens_cfg[2];
struct bmi2_sens_data sensor_data[2];

// ==== Wi-Fi AP ====
// Core2 receiver connects directly to this access point.
const char *AP_SSID = "AtomS3R_CAM";
const char *AP_PASSWORD = "123456";
constexpr uint8_t WIFI_CHANNEL = 3;
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

typedef struct struct_message {
  int16_t x;
  int16_t y;
  float ax, ay, az;
  float gx, gy, gz;
} struct_message;
struct_message myData;

uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

float ax, ay, az;
float gx, gy, gz;
float cx_smooth = 0;
float cy_smooth = 0;
bool first_point = true;
unsigned long last_imu_send_ms = 0;
constexpr unsigned long IMU_SEND_INTERVAL_MS = 100; // 10 Hz
unsigned long last_box_draw_ms = 0;
constexpr unsigned long BOX_DRAW_INTERVAL_MS = 100; // 10 Hz
constexpr bool ENABLE_RED_DETECTION = true;
constexpr long STREAM_SEND_TIMEOUT_US = 350000; // TCP詰まりを350 msで打ち切る
portMUX_TYPE imu_data_mux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t diag_espnow_ok = 0;
volatile uint32_t diag_espnow_fail = 0;
uint32_t diag_stream_frames = 0;
uint32_t diag_stream_bytes = 0;
uint32_t diag_max_encode_us = 0;
uint32_t diag_max_send_us = 0;
uint32_t diag_last_frame_ms = 0;
uint32_t diag_last_report_ms = 0;

// ==== Bosch API用のI2Cラッパ ====

// 書き込み
BMI2_INTF_RETURN_TYPE i2c_reg_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr) {
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(reg_addr);
  Wire.write(reg_data, length);
  return (Wire.endTransmission() == 0) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

// 読み込み
BMI2_INTF_RETURN_TYPE i2c_reg_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr) {
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(reg_addr);
  Wire.endTransmission(false);
  Wire.requestFrom((uint16_t)BMI270_ADDR, (uint8_t)length);
  for (uint32_t i = 0; i < length; i++) {
    reg_data[i] = Wire.read();
  }
  return BMI2_INTF_RET_SUCCESS;
}

void bmi2_delay_us(uint32_t period, void *intf_ptr) {
  delayMicroseconds(period);
}

// ==== BMI270 初期化 ====
void init_bmi270() {
  bmi2_dev.intf = BMI2_I2C_INTF;
  bmi2_dev.read = i2c_reg_read;
  bmi2_dev.write = i2c_reg_write;
  bmi2_dev.delay_us = bmi2_delay_us;
  bmi2_dev.intf_ptr = NULL;
  bmi2_dev.read_write_len = 32;
  bmi2_dev.config_file_ptr = NULL;

  int8_t rslt = bmi270_init(&bmi2_dev);
  if (rslt != BMI2_OK) {
    Serial.printf("BMI270 init failed: %d\n", rslt);
    return;
  }

  uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_GYRO };
  bmi2_sensor_enable(sens_list, 2, &bmi2_dev);

  sens_cfg[0].type = BMI2_ACCEL;
  sens_cfg[1].type = BMI2_GYRO;
  bmi2_get_sensor_config(sens_cfg, 2, &bmi2_dev);

  sens_cfg[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
  sens_cfg[0].cfg.acc.range = BMI2_ACC_RANGE_8G;
  sens_cfg[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
  sens_cfg[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;

  bmi2_set_sensor_config(sens_cfg, 2, &bmi2_dev);
  Serial.println("BMI270 init OK");
}

// ==== 加速度・ジャイロ読み出し（最新ライブラリ対応） ====
void readAccelGyro() {
  int8_t rslt;

  // LSB換算値
  const float ACC_LSB_8G   = 8.0f / 32768.0f;     // ±2G
  const float GYR_LSB_2000 = 2000.0f / 32768.0f;  // ±2000 dps

  // 加速度取得
  rslt = bmi2_get_sensor_data(&sensor_data[0], &bmi2_dev);
  if (rslt != BMI2_OK) return;
  ax = sensor_data[0].acc.x * ACC_LSB_8G;
  ay = sensor_data[0].acc.y * ACC_LSB_8G;
  az = sensor_data[0].acc.z * ACC_LSB_8G;

  // ジャイロ取得
  rslt = bmi2_get_sensor_data(&sensor_data[1], &bmi2_dev);
  if (rslt != BMI2_OK) return;
  gx = sensor_data[1].gyr.x * GYR_LSB_2000;
  gy = sensor_data[1].gyr.y * GYR_LSB_2000;
  gz = sensor_data[1].gyr.z * GYR_LSB_2000;
}

// ==== ESP-NOW送信コールバック ====
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) diag_espnow_ok++;
  else diag_espnow_fail++;
}

// ==== MJPEG ====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

uint8_t* out_jpg   = NULL;
size_t out_jpg_len = 0;

// ==== 緑の2画素枠を描画 ====
void draw_green_rect(camera_fb_t *fb, int x_min, int y_min, int x_max, int y_max) {
  if (x_min < 0 || y_min < 0 || x_max >= fb->width || y_max >= fb->height) return;
  uint16_t green = (63 << 5);
  for (int thickness = 0; thickness < 2; ++thickness) {
    const int left_x = x_min + thickness;
    const int right_x = x_max - thickness;
    const int top_y = y_min + thickness;
    const int bottom_y = y_max - thickness;
    if (left_x > right_x || top_y > bottom_y) break;

    for (int x = left_x; x <= right_x; ++x) {
      size_t top = (top_y * fb->width + x) * 2;
      size_t bottom = (bottom_y * fb->width + x) * 2;
      fb->buf[top] = fb->buf[bottom] = green >> 8;
      fb->buf[top + 1] = fb->buf[bottom + 1] = green & 0xFF;
    }
    for (int y = top_y; y <= bottom_y; ++y) {
      size_t left = (y * fb->width + left_x) * 2;
      size_t right = (y * fb->width + right_x) * 2;
      fb->buf[left] = fb->buf[right] = green >> 8;
      fb->buf[left + 1] = fb->buf[right + 1] = green & 0xFF;
    }
  }
}

// ==== 色検出処理 ====
void Color_detection(camera_fb_t *fb){
  int16_t detected_x = 0;
  int16_t detected_y = 0;
  if (ENABLE_RED_DETECTION) {
  int count[] = {0,0,0,0};
  int height = fb->height;
  int width  = fb->width;
  int y_min = 1000, y_max = -1000;
  int x_min = 1000, x_max = -1000;

  for(int y=0;y<height;y++){
    for(int x=0;x<width;x++){
      size_t idx=(y*width+x)*2;
      uint16_t pixel=(fb->buf[idx]<<8)|(fb->buf[idx+1]);
      uint8_t r=((pixel>>11)&0x1F)<<3;
      uint8_t g=((pixel>>5)&0x3F)<<2;
      uint8_t b=(pixel&0x1F)<<3;

      if(r>200 && g<100 && b<100){
        if(x<width/3) count[0]++; else if(x>width/3*2) count[1]++;
        if(y<height/3) count[2]++; else if(y>height/3*2) count[3]++;

        if(x_min>x) x_min=x; if(x_max<x) x_max=x;
        if(y_min>y) y_min=y; if(y_max<y) y_max=y;
      }
    }
  }

  if(x_min<x_max && y_min<y_max){
    int cx=(x_min+x_max)/2;
    int cy=(y_min+y_max)/2;

    if(first_point){
      cx_smooth=cx; cy_smooth=cy; first_point=false;
    } else {
      float alpha=0.2;
      cx_smooth=cx_smooth*(1-alpha)+cx*alpha;
      cy_smooth=cy_smooth*(1-alpha)+cy*alpha;
    }
    if (millis() - last_box_draw_ms >= BOX_DRAW_INTERVAL_MS) {
      draw_green_rect(fb, x_min, y_min, x_max, y_max);
      last_box_draw_ms = millis();
    }
    detected_x=(int)cx_smooth;
    detected_y=(int)cy_smooth;
  }
  }

  portENTER_CRITICAL(&imu_data_mux);
  myData.x = detected_x;
  myData.y = detected_y;
  portEXIT_CRITICAL(&imu_data_mux);
}

// HTTP送信が停止してもIMU/ESP-NOWを10 Hzで継続する。
void serviceImuEspNow() {
  const unsigned long now = millis();
  if (now - last_imu_send_ms < IMU_SEND_INTERVAL_MS) return;
  last_imu_send_ms = now;

  readAccelGyro();

  struct_message packet;
  portENTER_CRITICAL(&imu_data_mux);
  myData.ax=ax; myData.ay=ay; myData.az=az;
  myData.gx=gx; myData.gy=gy; myData.gz=gz;
  packet = myData;
  portEXIT_CRITICAL(&imu_data_mux);

  if (esp_now_send(broadcastAddress,(uint8_t*)&packet,sizeof(packet)) != ESP_OK) {
    diag_espnow_fail++;
  }
}

// ==== ストリームハンドラ ====
static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t *fb=NULL;
  esp_err_t res=ESP_OK;
  char part_buf[64];

  res=httpd_resp_set_type(req,_STREAM_CONTENT_TYPE);
  if(res!=ESP_OK) return res;

  // Core2側のTCP受信が詰まった場合、何秒もカメラ処理を止めず接続を閉じる。
  const int sockfd = httpd_req_to_sockfd(req);
  struct timeval send_timeout = {};
  send_timeout.tv_usec = STREAM_SEND_TIMEOUT_US;
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                 &send_timeout, sizeof(send_timeout)) != 0) {
    Serial.printf("[CAM][WARN] SO_SNDTIMEO failed errno=%d\n", errno);
  }

  const int frame_interval_ms=1000/30;

  while(true){
    unsigned long start_time=millis();
    if (diag_last_frame_ms != 0 && start_time - diag_last_frame_ms >= 500) {
      Serial.printf("[CAM][FRAME_GAP] gap=%lums heap=%u psram=%u\n",
                    start_time - diag_last_frame_ms, ESP.getFreeHeap(), ESP.getFreePsram());
    }
    diag_last_frame_ms = start_time;
    fb=esp_camera_fb_get();
    if(!fb){Serial.println("Camera capture failed"); res=ESP_FAIL;}
    
    Color_detection(fb);

    const uint32_t encode_start_us = micros();
    free(out_jpg);
    bool jpeg_converted=frame2jpg(fb,80,&out_jpg,&out_jpg_len);
    const uint32_t encode_us = micros() - encode_start_us;
    if (encode_us > diag_max_encode_us) diag_max_encode_us = encode_us;
    esp_camera_fb_return(fb);
    fb=NULL;
    if(!jpeg_converted){Serial.println("JPEG compression failed"); res=ESP_FAIL;}

    const uint32_t send_start_us = micros();
    if(res==ESP_OK){
      size_t hlen=snprintf(part_buf,64,_STREAM_PART,out_jpg_len);
      res=httpd_resp_send_chunk(req,part_buf,hlen);
    }
    if(res==ESP_OK) res=httpd_resp_send_chunk(req,(const char*)out_jpg,out_jpg_len);
    if(res==ESP_OK) res=httpd_resp_send_chunk(req,_STREAM_BOUNDARY,strlen(_STREAM_BOUNDARY));
    const uint32_t send_us = micros() - send_start_us;
    if (send_us > diag_max_send_us) diag_max_send_us = send_us;
    if (res != ESP_OK) {
      Serial.printf("[CAM][SEND_ABORT] wait=%luus err=0x%x\n",
                    (unsigned long)send_us, (unsigned int)res);
    }
    diag_stream_frames++;
    diag_stream_bytes += out_jpg_len;

    const uint32_t report_now = millis();
    if (report_now - diag_last_report_ms >= 1000) {
      Serial.printf("[CAM][STAT] fps=%lu kb=%lu encMax=%luus sendMax=%luus "
                    "nowOK=%lu nowFail=%lu heap=%u psram=%u\n",
                    (unsigned long)diag_stream_frames,
                    (unsigned long)(diag_stream_bytes / 1024),
                    (unsigned long)diag_max_encode_us,
                    (unsigned long)diag_max_send_us,
                    (unsigned long)diag_espnow_ok,
                    (unsigned long)diag_espnow_fail,
                    ESP.getFreeHeap(), ESP.getFreePsram());
      diag_stream_frames = 0;
      diag_stream_bytes = 0;
      diag_max_encode_us = 0;
      diag_max_send_us = 0;
      diag_espnow_ok = 0;
      diag_espnow_fail = 0;
      diag_last_report_ms = report_now;
    }
    if(res!=ESP_OK) break;

    unsigned long elapsed=millis()-start_time;
    if(elapsed<frame_interval_ms) vTaskDelay((frame_interval_ms-elapsed)/portTICK_PERIOD_MS);
  }
  return res;
}

// ==== MJPEGサーバ開始 ====
void start_MJPEG_server(){
  httpd_config_t config=HTTPD_DEFAULT_CONFIG();
  config.server_port=80;
  config.uri_match_fn=httpd_uri_match_wildcard;
  config.max_open_sockets=8;
  config.stack_size=8192;
  config.lru_purge_enable=true;
  config.send_wait_timeout=1;

  httpd_handle_t httpd=NULL;
  if(httpd_start(&httpd,&config)==ESP_OK){
    httpd_uri_t stream_uri={ .uri="/video", .method=HTTP_GET, .handler=stream_handler, .user_ctx=NULL };
    httpd_register_uri_handler(httpd,&stream_uri);
  }
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 2, 1);
  Serial2.println("ATOMS3R-CAM");

  Wire.begin(45,0,400000);
  init_bmi270();

  pinMode(18,OUTPUT);
  digitalWrite(18,LOW);
  delay(500);

  camera_config_t config;
  config.ledc_channel=LEDC_CHANNEL_0;
  config.ledc_timer=LEDC_TIMER_0;
  config.pin_d0=3;
  config.pin_d1=42;
  config.pin_d2=46;
  config.pin_d3=48;
  config.pin_d4=4;
  config.pin_d5=17;
  config.pin_d6=11;
  config.pin_d7=13;
  config.pin_xclk=21;
  config.pin_pclk=40;
  config.pin_vsync=10;
  config.pin_href=14;
  config.pin_sccb_sda=12;
  config.pin_sccb_scl=9;
  config.pin_pwdn=-1;
  config.pin_reset=-1;
  config.xclk_freq_hz=25000000;
  config.pixel_format=PIXFORMAT_RGB565;
  config.frame_size=FRAMESIZE_QQVGA;
  config.jpeg_quality=30;
  config.fb_count=3;
  config.grab_mode=CAMERA_GRAB_LATEST;
  config.fb_location=CAMERA_FB_IN_PSRAM;

  esp_err_t err=esp_camera_init(&config);
  if(err!=ESP_OK){Serial.printf("Camera init failed with error 0x%x",err); return;}
  sensor_t *s=esp_camera_sensor_get();
  s->set_hmirror(s,0);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, WIFI_CHANNEL)) {
    Serial.println("Failed to start Wi-Fi AP");
    return;
  }
  Serial.printf("Wi-Fi AP started: %s\n", AP_SSID);
  Serial.printf("Stream URL: http://%s/video\n", WiFi.softAPIP().toString().c_str());

  start_MJPEG_server();

  if(esp_now_init()!=ESP_OK){Serial.println("Error initializing ESP-NOW"); return;}
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo={};
  memcpy(peerInfo.peer_addr,broadcastAddress,6);
  peerInfo.channel=WIFI_CHANNEL;
  peerInfo.ifidx=WIFI_IF_AP;
  peerInfo.encrypt=false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW broadcast peer");
  }
}

void loop() {
  serviceImuEspNow();
  delay(1);
}
