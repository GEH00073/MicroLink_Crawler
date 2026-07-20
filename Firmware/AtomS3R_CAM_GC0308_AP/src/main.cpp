#include <Wire.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_camera.h>
#include <esp_now.h>

// ==== BMI270 (Bosch API) ====
#include "bmi270.h"
#include "bmi2.h"

#define BMI270_ADDR 0x68

struct bmi2_dev bmi2_dev;
struct bmi2_sens_config sens_cfg[2];
struct bmi2_sens_data sensor_data[2];

// APモード設定
const char* ap_ssid = "ATOMS3R_CAM";
const char* ap_password = "30023002aa";  // 8文字以上
int wifiChannel = 3;  // 好きなチャンネル番号（1〜13）2〜5,9〜10が空いてる?

// 固定IPを設定する場合
IPAddress local_IP(192,168,4,1);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);


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
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

// ==== MJPEG ====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

uint8_t* out_jpg   = NULL;
size_t out_jpg_len = 0;

// ==== 緑丸を描画 ====
void draw_green_circle(camera_fb_t *fb, int cx, int cy, int radius) {
  if (cx < 0 || cy < 0 || cx >= fb->width || cy >= fb->height) return;
  uint16_t green = (63 << 5);
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        int px = cx + x;
        int py = cy + y;
        if (px >= 0 && px < fb->width && py >= 0 && py < fb->height) {
          size_t pixel_index = (py * fb->width + px) * 2;
          fb->buf[pixel_index]     = green >> 8;
          fb->buf[pixel_index + 1] = green & 0xFF;
        }
      }
    }
  }
}

// ==== 色検出処理 ====
void Color_detection(camera_fb_t *fb){
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
    draw_green_circle(fb,(int)cx_smooth,(int)cy_smooth,3);
    myData.x=(int)cx_smooth;
    myData.y=(int)cy_smooth;
  } else {
    myData.x=0; myData.y=0;
  }

  // BMI270データ格納
  myData.ax=ax; myData.ay=ay; myData.az=az;
  myData.gx=gx; myData.gy=gy; myData.gz=gz;

  // ESP-NOW送信
  esp_now_send(broadcastAddress,(uint8_t*)&myData,sizeof(myData));
}

// ==== ストリームハンドラ ====
static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t *fb=NULL;
  esp_err_t res=ESP_OK;
  char part_buf[64];

  res=httpd_resp_set_type(req,_STREAM_CONTENT_TYPE);
  if(res!=ESP_OK) return res;

  const int frame_interval_ms=1000/30;

  while(true){
    unsigned long start_time=millis();
    fb=esp_camera_fb_get();
    if(!fb){Serial.println("Camera capture failed"); res=ESP_FAIL;}
    
    readAccelGyro();
    // Serial出力
    Serial2.printf("a:%.2f %.2f %.2f ,g:%.2f %.2f %.2f\n", ax, ay, az, gx, gy, gz);
    // Serial2.printf("g:%.2f %.2f %.2f,\n", gx, gy, gz);
    // Serial2.printf("%d %d %d %d \n", x_min, y_min, x_max - x_min, y_max - y_min);

    Color_detection(fb);

    free(out_jpg);
    bool jpeg_converted=frame2jpg(fb,80,&out_jpg,&out_jpg_len);
    esp_camera_fb_return(fb);
    fb=NULL;
    if(!jpeg_converted){Serial.println("JPEG compression failed"); res=ESP_FAIL;}

    if(res==ESP_OK){
      size_t hlen=snprintf(part_buf,64,_STREAM_PART,out_jpg_len);
      res=httpd_resp_send_chunk(req,part_buf,hlen);
    }
    if(res==ESP_OK) res=httpd_resp_send_chunk(req,(const char*)out_jpg,out_jpg_len);
    if(res==ESP_OK) res=httpd_resp_send_chunk(req,_STREAM_BOUNDARY,strlen(_STREAM_BOUNDARY));
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

  httpd_handle_t httpd=NULL;
  if(httpd_start(&httpd,&config)==ESP_OK){
    httpd_uri_t stream_uri={ .uri="/", .method=HTTP_GET, .handler=stream_handler, .user_ctx=NULL };
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

  // APモード起動
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password, wifiChannel);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  start_MJPEG_server();

  if(esp_now_init()!=ESP_OK){Serial.println("Error initializing ESP-NOW"); return;}
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo={};
  memcpy(peerInfo.peer_addr,broadcastAddress,6);
  peerInfo.channel = wifiChannel;  
  peerInfo.encrypt=false;
  esp_now_add_peer(&peerInfo);
}

void loop() {}
