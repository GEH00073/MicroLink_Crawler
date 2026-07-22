#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h> 
#include <esp_now.h>
#include <esp_wifi.h>

// ===== AtomS3R-CAM AP設定 =====
const char* ssid     = "AtomS3R_CAM";
const char* password = "123456";

// ===== ストリーミングサーバのURL =====
String streamURL = "http://192.168.4.1/video";


// バッファサイズ
#define STREAM_BUFFER_SIZE (300 * 1024)
// 1: Core 0で受信、Core 1で描画 / 0: 従来の単一ループ方式
#define USE_DUAL_CORE_STREAMING 0

static LGFX_Sprite canvas(&M5.Display);   // オフスクリーン描画用バッファ
static LGFX_Sprite text(&M5.Display);   // テキスト描画用バッファ
static LGFX_Sprite lamp(&M5.Display);   // ランプ描画用バッファ

WiFiClient client;
HTTPClient http;
uint8_t* jpgBuf;
size_t jpgBufLen;
uint8_t* latestJpgBuf;
size_t latestJpgLen = 0;
bool latestJpgReady = false;
#if USE_DUAL_CORE_STREAMING
uint8_t* displayJpgBuf;
size_t displayJpgLen = 0;
SemaphoreHandle_t frameBufferMutex = nullptr;
TaskHandle_t streamReceiveTaskHandle = nullptr;
TaskHandle_t streamDisplayTaskHandle = nullptr;
#endif

u_long ptime, ptime2;
int fps;
float angleAccum = 0; // 累積回転角（ラジアン）
unsigned long lastAngleUpdateMs = 0;
constexpr unsigned long IMU_UPDATE_INTERVAL_MS = 100; // 10 Hz

volatile uint32_t diagEspNowPackets = 0;
volatile uint32_t diagLastEspNowMs = 0;
uint32_t diagRxBytes = 0;
uint32_t diagJpegFrames = 0;
uint32_t diagDrawFrames = 0;
uint32_t diagDroppedFrames = 0;
uint32_t diagLastTcpRxMs = 0;
uint32_t diagLastJpegMs = 0;
uint32_t diagLastDrawMs = 0;
uint32_t diagMaxDecodeUs = 0;
uint32_t diagMaxPushUs = 0;
uint32_t diagLastReportMs = 0;
bool diagTcpStallReported = false;

constexpr uint32_t STREAM_STALL_RECONNECT_MS = 2500;
constexpr uint32_t STREAM_START_TIMEOUT_MS = 1500;
constexpr uint32_t STREAM_RECONNECT_RETRY_MS = 500;
uint32_t streamConnectedMs = 0;
uint32_t lastStreamConnectAttemptMs = 0;
uint32_t streamReconnectCount = 0;

// JPEG処理用の状態管理
bool inJPEG = false;
size_t idx = 0;
uint8_t jpegPreviousByte = 0;

#if USE_DUAL_CORE_STREAMING
void streamReceiveTask(void* parameter);
void streamDisplayTask(void* parameter);
#endif

// === esp-nowデータ構造体 ===
typedef struct struct_message {
  int16_t x;
  int16_t y;
  float ax, ay, az;
  float gx, gy, gz;
} struct_message;

struct_message incomingData;

// === ESP-NOW 受信コールバック ===
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataBuf, int len) {
  if (len != sizeof(incomingData)) return;
  memcpy(&incomingData, incomingDataBuf, sizeof(incomingData));
  diagEspNowPackets++;
  diagLastEspNowMs = millis();
//   Serial.printf("ESP-NOW RECV -> x:%d y:%d  ax:%.2f ay:%.2f az:%.2f  gx:%.2f gy:%.2f gz:%.2f\n",
//                 incomingData.x, incomingData.y,
//                 incomingData.ax, incomingData.ay, incomingData.az,
//                 incomingData.gx, incomingData.gy, incomingData.gz);
}

// ==== IMU 表示用 ====
int imuLine = 50;   // Y座標開始位置

// ==== JPEG/IMU受信状態管理 ====
bool inHeader = true;       // ヘッダ読み取り中フラグ
String headerLine = "";     // ヘッダ格納用

// =======================
// JPEG描画コールバック
// =======================
static bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h,
                       uint16_t* bitmap) {
    if (y >= M5.Display.height()) return 0;
    canvas.pushImage(x, y, w, h, bitmap);
    // M5.Display.pushImage(x, y, w, h, bitmap);
    return 1;
}

// ===== 中央十字線描画 =====
void drawCrosshair() {
    int cx = canvas.width() / 2;
    int cy = canvas.height() / 2;
    int len = 11;
    int gap = 4;

    // 横線
    canvas.drawLine(cx - len, cy - 1, cx - gap, cy - 1, TFT_BLACK);  // 黒影
    canvas.drawLine(cx + gap, cy - 1, cx + len, cy - 1, TFT_BLACK);
    canvas.drawLine(cx - len, cy, cx - gap, cy, TFT_WHITE);          // 白線
    canvas.drawLine(cx + gap, cy, cx + len, cy, TFT_WHITE);

    // 縦線
    canvas.drawLine(cx + 1, cy - len, cx + 1, cy - gap, TFT_BLACK);  // 黒影
    canvas.drawLine(cx + 1, cy + gap, cx + 1, cy + len, TFT_BLACK);
    canvas.drawLine(cx, cy - len, cx, cy - gap, TFT_WHITE);          // 白線
    canvas.drawLine(cx, cy + gap, cx, cy + len, TFT_WHITE);
}

void resetStreamParser() {
    inJPEG = false;
    idx = 0;
    jpegPreviousByte = 0;
    latestJpgLen = 0;
    latestJpgReady = false;
}

bool connectCameraStream() {
    lastStreamConnectAttemptMs = millis();
    http.end();
    client.stop();
    resetStreamParser();

    if (WiFi.status() != WL_CONNECTED) return false;

    http.setReuse(false);
    http.setConnectTimeout(1000);
    http.setTimeout(1000);
    if (!http.begin(client, streamURL)) {
        Serial.println("[CORE][RECONNECT_FAIL] http.begin");
        return false;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[CORE][RECONNECT_FAIL] HTTP=%d\n", httpCode);
        http.end();
        client.stop();
        return false;
    }

    client.setNoDelay(true);
    streamConnectedMs = millis();
    diagLastTcpRxMs = 0;
    diagTcpStallReported = false;
    streamReconnectCount++;
    Serial.printf("[CORE][STREAM_CONNECTED] count=%lu\n",
                  (unsigned long)streamReconnectCount);
    return true;
}

void serviceStreamConnection() {
    const uint32_t now = millis();
    const bool hasNoPendingData = client.available() == 0;
    const bool receiveStalled = diagLastTcpRxMs != 0 && hasNoPendingData &&
                                now - diagLastTcpRxMs >= STREAM_STALL_RECONNECT_MS;
    const bool startTimedOut = diagLastTcpRxMs == 0 && streamConnectedMs != 0 &&
                               now - streamConnectedMs >= STREAM_START_TIMEOUT_MS;
    const bool disconnected = !client.connected();

    if (!receiveStalled && !startTimedOut && !disconnected) return;
    if (now - lastStreamConnectAttemptMs < STREAM_RECONNECT_RETRY_MS) return;

    Serial.printf("[CORE][RECONNECT] reason=%s rxAge=%lums conn=%d wifi=%d\n",
                  disconnected ? "closed" : (startTimedOut ? "start" : "stall"),
                  diagLastTcpRxMs == 0 ? 0UL : (unsigned long)(now - diagLastTcpRxMs),
                  client.connected(), WiFi.status());
    connectCameraStream();
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(200);      

    canvas.setPsram(false);
    canvas.createSprite(120, 120); // メモリ確保
    canvas.setSwapBytes(true); // スワップON(色がおかしい場合には変更する)
    canvas.setPivot(60, 60);

    text.setPsram(true);
    text.createSprite(80, 160); // メモリ確保
    text.setSwapBytes(false); // スワップON(色がおかしい場合には変更する)
    text.setPivot(0, 0);
    text.setTextFont(1);
    text.setTextColor(0xFFFF, 0x0000);  // 白文字、黒背景

    lamp.setPsram(true);
    lamp.createSprite(80, 48); // メモリ確保
    lamp.setSwapBytes(false);
    lamp.setPivot(0, 0);
    lamp.setTextFont(1);
    lamp.setTextColor(0xFFFF, 0x0000);
   

    // TJpg_Decoder設定
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tft_output);

    // AtomS3R-CAM が起動するアクセスポイントへ接続する。
    // 接続先APのチャネルをそのまま使うため、ESP-NOWも同じチャネルで動作する。
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    M5.Display.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        M5.Display.print(".");
    }
    M5.Display.println("\nWiFi Connected!");
    M5.Display.println(WiFi.localIP());
    esp_wifi_set_ps(WIFI_PS_NONE);

    // HTTP接続開始。切断後も同じ関数でJPEG解析状態を初期化して再接続する。
    if (connectCameraStream()) M5.Display.println("Connected to camera stream.");
    else M5.Display.println("HTTP connect failed");

    jpgBuf = (uint8_t*)malloc(STREAM_BUFFER_SIZE);
    latestJpgBuf = (uint8_t*)malloc(STREAM_BUFFER_SIZE);
#if USE_DUAL_CORE_STREAMING
    displayJpgBuf = (uint8_t*)malloc(STREAM_BUFFER_SIZE);
#endif
    jpgBufLen = 0;
    if (jpgBuf == nullptr || latestJpgBuf == nullptr
#if USE_DUAL_CORE_STREAMING
        || displayJpgBuf == nullptr
#endif
    ) {
        Serial.println("JPEG buffer allocation failed");
        return;
    }

    M5.Display.clear();

    // ===== ESP-NOW 初期化 =====
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);

#if USE_DUAL_CORE_STREAMING
    frameBufferMutex = xSemaphoreCreateMutex();
    if (frameBufferMutex == nullptr) {
        Serial.println("Frame buffer mutex creation failed");
        return;
    }
    if (xTaskCreatePinnedToCore(streamReceiveTask, "stream_rx", 8192, nullptr, 3,
                                &streamReceiveTaskHandle, 0) != pdPASS) {
        Serial.println("Stream receive task creation failed");
        return;
    }
    if (xTaskCreatePinnedToCore(streamDisplayTask, "stream_display", 12288, nullptr, 2,
                                &streamDisplayTaskHandle, 1) != pdPASS) {
        Serial.println("Stream display task creation failed");
        return;
    }
#endif
}

uint16_t lineColors[9] = {
    TFT_GREEN,      // 1行目: 緑
    TFT_YELLOW,     // 2行目: イエロー
    TFT_MAGENTA,    // 3行目: マゼンタ
    TFT_CYAN,       // 4行目: シアン
    TFT_RED,        // 5行目: 赤
    TFT_GREEN,      // 6行目: 緑
    TFT_SKYBLUE,       // 7行目: 青
    TFT_ORANGE,     // 8行目: オレンジ
    TFT_SKYBLUE     // 9行目: 明るい水色
};

#if USE_DUAL_CORE_STREAMING
void processStreamBytes(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        const uint8_t byte = data[i];

        if (!inJPEG) {
            if (jpegPreviousByte == 0xFF && byte == 0xD8) {
                idx = 0;
                jpgBuf[idx++] = 0xFF;
                jpgBuf[idx++] = 0xD8;
                inJPEG = true;
            }
            jpegPreviousByte = byte;
            continue;
        }

        if (idx >= STREAM_BUFFER_SIZE) {
            idx = 0;
            inJPEG = false;
            jpegPreviousByte = byte;
            continue;
        }

        jpgBuf[idx++] = byte;
        const bool frameComplete = jpegPreviousByte == 0xFF && byte == 0xD9;
        jpegPreviousByte = byte;
        if (!frameComplete) continue;

        xSemaphoreTake(frameBufferMutex, portMAX_DELAY);
        if (latestJpgReady) diagDroppedFrames++;
        uint8_t* completedJpg = jpgBuf;
        jpgBuf = latestJpgBuf;
        latestJpgBuf = completedJpg;
        latestJpgLen = idx;
        latestJpgReady = true;
        xSemaphoreGive(frameBufferMutex);
        diagJpegFrames++;
        diagLastJpegMs = millis();

        idx = 0;
        inJPEG = false;
    }
}

void streamReceiveTask(void* parameter) {
    uint8_t receiveBuffer[2048];
    while (true) {
        const int availableBytes = client.available();
        if (client.connected() && availableBytes > 0) {
            const size_t readSize = min<size_t>(sizeof(receiveBuffer), availableBytes);
            const int len = client.read(receiveBuffer, readSize);
            if (len > 0) {
                const uint32_t now = millis();
                diagRxBytes += len;
                diagLastTcpRxMs = now;
                diagTcpStallReported = false;
                processStreamBytes(receiveBuffer, len);
                continue;
            }
        }
        vTaskDelay(1);
    }
}
#endif

void drawLatestFrame() {
    uint8_t* frameBuffer = latestJpgBuf;
    size_t frameLength = latestJpgLen;

#if USE_DUAL_CORE_STREAMING
    xSemaphoreTake(frameBufferMutex, portMAX_DELAY);
    if (!latestJpgReady) {
        xSemaphoreGive(frameBufferMutex);
        return;
    }
    uint8_t* previousDisplayBuffer = displayJpgBuf;
    displayJpgBuf = latestJpgBuf;
    latestJpgBuf = previousDisplayBuffer;
    displayJpgLen = latestJpgLen;
    latestJpgReady = false;
    frameBuffer = displayJpgBuf;
    frameLength = displayJpgLen;
    xSemaphoreGive(frameBufferMutex);
#else
    if (!latestJpgReady) return;
#endif

    const uint32_t decodeStartUs = micros();
    TJpgDec.drawJpg(0, 0, frameBuffer, frameLength);
    const uint32_t decodeUs = micros() - decodeStartUs;
    if (decodeUs > diagMaxDecodeUs) diagMaxDecodeUs = decodeUs;

    const uint32_t pushStartUs = micros();
    drawCrosshair();
    canvas.pushRotateZoom(118, 108, 270, 1.5, 1.5);
    const uint32_t pushUs = micros() - pushStartUs;
    if (pushUs > diagMaxPushUs) diagMaxPushUs = pushUs;
    diagDrawFrames++;
    diagLastDrawMs = millis();

    static size_t lastJpgSize = 0;
    if (frameLength != lastJpgSize) {
        fps++;
        lastJpgSize = frameLength;
    }

    const unsigned long now = millis();
    if (now - lastAngleUpdateMs >= IMU_UPDATE_INTERVAL_MS) {
        const float dt = lastAngleUpdateMs == 0
            ? IMU_UPDATE_INTERVAL_MS / 1000.0f
            : (now - lastAngleUpdateMs) / 1000.0f;
        if (incomingData.gy > 0.1f || incomingData.gy < -0.1f) {
            angleAccum += incomingData.gy * PI / 180.0f * dt;
        }
        lastAngleUpdateMs = now;
    }

    if (now - ptime >= IMU_UPDATE_INTERVAL_MS) {
        text.clear();
        text.setCursor(0, 10);
        text.setTextColor(TFT_WHITE, TFT_BLACK);
        text.printf("fps: %.1f\n", fps * 10.0);
        text.setCursor(0, 20); text.setTextColor(TFT_CYAN, TFT_BLACK);
        text.printf("x: %d\n", incomingData.y);
        text.setCursor(0, 30); text.setTextColor(TFT_ORANGE, TFT_BLACK);
        text.printf("y: %d\n", incomingData.x);
        text.setCursor(0, 40); text.setTextColor(TFT_SKYBLUE, TFT_BLACK);
        text.printf("ax: %.3f\n", incomingData.ax);
        text.setCursor(0, 50); text.setTextColor(TFT_YELLOW, TFT_BLACK);
        text.printf("ay: %.3f\n", incomingData.ay);
        text.setCursor(0, 60); text.setTextColor(TFT_MAGENTA, TFT_BLACK);
        text.printf("az: %.3f\n", incomingData.az);

        const int frameX = 30, frameY = 110, radius = 30;
        text.drawCircle(frameX, frameY, radius, TFT_GREEN);
        const int arrowX = frameX + int(cos(angleAccum) * radius);
        const int arrowY = frameY + int(sin(angleAccum) * radius);
        text.drawLine(frameX, frameY, arrowX, arrowY, TFT_YELLOW);
        const float headAngle = PI / 6;
        const int hx1 = arrowX - int(cos(angleAccum - headAngle) * 10);
        const int hy1 = arrowY - int(sin(angleAccum - headAngle) * 10);
        const int hx2 = arrowX - int(cos(angleAccum + headAngle) * 10);
        const int hy2 = arrowY - int(sin(angleAccum + headAngle) * 10);
        text.drawLine(arrowX, arrowY, hx1, hy1, TFT_YELLOW);
        text.drawLine(arrowX, arrowY, hx2, hy2, TFT_YELLOW);
        text.pushSprite(241, 10);
        fps = 0;
        ptime = now;
        M5.Display.setCursor(43, 225);
        M5.Display.print("Auto             Serch             Zoom");
    }

    if (now - ptime2 >= 500) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 5; c++) {
                const uint16_t color = random(0, 3) == 0 ? TFT_BLACK : random(0x0001, 0xFFFF);
                lamp.fillRect(c * 12, r * 12, 11, 11, color);
            }
        }
        lamp.pushSprite(241, 170);
        ptime2 = now;
    }
#if !USE_DUAL_CORE_STREAMING
    latestJpgReady = false;
#endif
}

#if USE_DUAL_CORE_STREAMING
void streamDisplayTask(void* parameter) {
    while (true) {
        drawLatestFrame();
        vTaskDelay(1);
    }
}
#endif

void reportCoreDiagnostics() {
    const uint32_t now = millis();
    const uint32_t rxAge = diagLastTcpRxMs == 0 ? 0 : now - diagLastTcpRxMs;
    const uint32_t jpegAge = diagLastJpegMs == 0 ? 0 : now - diagLastJpegMs;
    const uint32_t drawAge = diagLastDrawMs == 0 ? 0 : now - diagLastDrawMs;
    const uint32_t nowAge = diagLastEspNowMs == 0 ? 0 : now - diagLastEspNowMs;

    if (diagLastTcpRxMs != 0 && rxAge >= 500 && !diagTcpStallReported) {
        Serial.printf("[CORE][TCP_STALL] age=%lums jpegAge=%lums drawAge=%lums "
                      "avail=%d conn=%d wifi=%d rssi=%d heap=%u psram=%u\n",
                      (unsigned long)rxAge, (unsigned long)jpegAge,
                      (unsigned long)drawAge, client.available(), client.connected(),
                      WiFi.status(), WiFi.RSSI(), ESP.getFreeHeap(), ESP.getFreePsram());
        diagTcpStallReported = true;
    }

    if (now - diagLastReportMs >= 1000) {
        Serial.printf("[CORE][STAT] rxKB=%lu jpeg=%lu draw=%lu drop=%lu "
                      "decMax=%luus pushMax=%luus rxAge=%lums jpegAge=%lums "
                      "drawAge=%lums nowRx=%lu nowAge=%lums avail=%d rssi=%d "
                      "heap=%u psram=%u\n",
                      (unsigned long)(diagRxBytes / 1024),
                      (unsigned long)diagJpegFrames,
                      (unsigned long)diagDrawFrames,
                      (unsigned long)diagDroppedFrames,
                      (unsigned long)diagMaxDecodeUs,
                      (unsigned long)diagMaxPushUs,
                      (unsigned long)rxAge,
                      (unsigned long)jpegAge,
                      (unsigned long)drawAge,
                      (unsigned long)diagEspNowPackets,
                      (unsigned long)nowAge,
                      client.available(), WiFi.RSSI(),
                      ESP.getFreeHeap(), ESP.getFreePsram());
        diagRxBytes = 0;
        diagJpegFrames = 0;
        diagDrawFrames = 0;
        diagDroppedFrames = 0;
        diagMaxDecodeUs = 0;
        diagMaxPushUs = 0;
        diagEspNowPackets = 0;
        diagLastReportMs = now;
    }
}

void loop() {
    M5.update();
    serviceStreamConnection();
    // タッチがあるか確認
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
        int x = t.x;
        int y = t.y;
        int p = t.isPressed();
        M5.Speaker.setVolume(30);
        if(y > 240){
            if(x < 100){
                M5.Speaker.tone(1000, 100);
            }else if(x > 250){
                M5.Speaker.tone(3000, 100);
            }else{
                M5.Speaker.tone(2000, 100);  
            }
        }
        // M5.Display.setCursor(0, 0);
        // M5.Display.printf("x = %4d, y = %4d, press = %d", x, y, p);
    }

#if USE_DUAL_CORE_STREAMING
    // TCP受信と描画は専用タスクが担当し、loopは入力処理だけを行う。
    vTaskDelay(1);
#else
    if (client.connected() && client.available()) {
        // 受信ブロックの境界に依存せず、JPEGのSOI/EOIで正確に切り出す。
        // 一度に最大64KBまで処理し、その中で最後に完成したJPEGだけを描画する。
        size_t receiveBudget = 64 * 1024;
        while (client.available() && receiveBudget > 0) {
            uint8_t receiveBuffer[2048];
            const size_t readSize = min<size_t>(sizeof(receiveBuffer), receiveBudget);
            int len = client.read(receiveBuffer, readSize);
        if (len > 0) {
            const uint32_t rxNow = millis();
            if (diagTcpStallReported) {
                Serial.printf("[CORE][RX_RESUME] gap=%lums avail=%d rssi=%d\n",
                              (unsigned long)(rxNow - diagLastTcpRxMs),
                              client.available(), WiFi.RSSI());
                diagTcpStallReported = false;
            }
            diagRxBytes += len;
            diagLastTcpRxMs = rxNow;
            receiveBudget -= len;
            for (int i = 0; i < len; ++i) {
                const uint8_t byte = receiveBuffer[i];

                if (!inJPEG) {
                    if (jpegPreviousByte == 0xFF && byte == 0xD8) {
                        idx = 0;
                        jpgBuf[idx++] = 0xFF;
                        jpgBuf[idx++] = 0xD8;
                        inJPEG = true;
                    }
                    jpegPreviousByte = byte;
                    continue;
                }

                if (idx >= STREAM_BUFFER_SIZE) {
                    Serial.println("JPEG buffer overflow; discarding frame");
                    idx = 0;
                    inJPEG = false;
                    jpegPreviousByte = byte;
                    continue;
                }

                jpgBuf[idx++] = byte;
                const bool frameComplete = jpegPreviousByte == 0xFF && byte == 0xD9;
                jpegPreviousByte = byte;
                if (frameComplete) {
                    // 完成JPEGは最新フレーム用バッファと入れ替える。
                    // まだ描画していない古いフレームは、この時点で破棄される。
                    if (latestJpgReady) diagDroppedFrames++;
                    uint8_t* completedJpg = jpgBuf;
                    jpgBuf = latestJpgBuf;
                    latestJpgBuf = completedJpg;
                    latestJpgLen = idx;
                    latestJpgReady = true;
                    diagJpegFrames++;
                    diagLastJpegMs = millis();
                    idx = 0;
                    inJPEG = false;
                    continue;

                    // JPEG 1枚完成
                    TJpgDec.drawJpg(0, 0, jpgBuf, idx);
                    drawCrosshair();       // canvas上に中央十字描画
                    canvas.pushRotateZoom(118, 108, 270, 1.5, 1.5);
                    // canvas.pushSprite(0, 0);

                    static size_t last_jpg_size = 0;
                    if (idx != last_jpg_size) {
                        fps++;
                        last_jpg_size = idx;
                    }

                    const unsigned long now = millis();
                    if (now - lastAngleUpdateMs >= IMU_UPDATE_INTERVAL_MS) {
                        const float dt = lastAngleUpdateMs == 0
                            ? IMU_UPDATE_INTERVAL_MS / 1000.0f
                            : (now - lastAngleUpdateMs) / 1000.0f;
                        if (incomingData.gy > 0.1f || incomingData.gy < -0.1f) {
                            angleAccum += incomingData.gy * PI / 180.0f * dt;
                        }
                        lastAngleUpdateMs = now;
                    }

                    // Text
                    if(now - ptime >= IMU_UPDATE_INTERVAL_MS){
                        text.clear();
                        text.setCursor(0,10);
                        text.setTextColor(TFT_WHITE, TFT_BLACK); 
                        text.printf("fps: %.1f \n", fps * 10.0);
                        // ===== ESP-NOWで受信したデータの表示 =====
                        text.setCursor(0,20);
                        text.setTextColor(TFT_CYAN, TFT_BLACK);
                        text.printf("x: %d\n", incomingData.y);
                        text.setCursor(0,30);
                        text.setTextColor(TFT_ORANGE, TFT_BLACK);
                        text.printf("y: %d\n", incomingData.x);
                        text.setCursor(0,40);
                        text.setTextColor(TFT_SKYBLUE, TFT_BLACK);
                        text.printf("ax: %.3f\n", incomingData.ax);
                        text.setCursor(0,50);
                        text.setTextColor(TFT_YELLOW, TFT_BLACK);
                        text.printf("ay: %.3f\n", incomingData.ay);
                        text.setCursor(0,60);
                        text.setTextColor(TFT_MAGENTA, TFT_BLACK);
                        text.printf("az: %.3f\n", incomingData.az);

                        // // ===== 枠とドット描画 =====
                        // int frameX = 0;   // 枠の左上X
                        // int frameY = 80;    // 枠の左上Y（y表示の下あたり）
                        // int frameSize = 60; // 枠の大きさ

                        // // 枠の描画
                        // text.drawRect(frameX, frameY, frameSize, frameSize, TFT_GREEN);

                        // if(incomingData.x !=0){
                        //     // === 270度回転 ===
                        //     int rotX = int(incomingData.y / 120.0 * 60.0);
                        //     int rotY = int((160 - incomingData.x -40) / 160.0 * 60.0);
                        //     // 4x4のドットを描画
                        //     text.fillRect(frameX + rotX, frameY + rotY, 4, 4, TFT_RED);
                        // }
                        // ===== 円と累積回転矢印描画 =====
                    int frameX = 30;      // 円の中心X
                    int frameY = 110;     // 円の中心Y
                    int radius = 30;      // 円の半径
                    // static float angleAccum = 0; // 累積回転角（ラジアン）

                    // 円の描画
                    text.drawCircle(frameX, frameY, radius, TFT_GREEN);

                    // gy の累積による回転（gx > 10 の場合のみ）
                    // if (incomingData.gx > 0.1 || incomingData.gx < -0.1) {
                    //     angleAccum += incomingData.gy * PI / 180.0f / 4.0; // gyを度からラジアンに変換して累積
                    // }
                        int arrowLength = radius;        // 矢の長さ
                        int arrowX = frameX + int(cos(angleAccum) * arrowLength);
                        int arrowY = frameY + int(sin(angleAccum) * arrowLength);

                        // 矢本体
                        text.drawLine(frameX, frameY, arrowX, arrowY, TFT_YELLOW);

                        // 矢じりを大きくする
                        int arrowHeadSize = 10;          // 矢じりサイズ
                        float headAngle = PI / 6;        // 30度
                        int hx1 = arrowX - int(cos(angleAccum - headAngle) * arrowHeadSize);
                        int hy1 = arrowY - int(sin(angleAccum - headAngle) * arrowHeadSize);
                        int hx2 = arrowX - int(cos(angleAccum + headAngle) * arrowHeadSize);
                        int hy2 = arrowY - int(sin(angleAccum + headAngle) * arrowHeadSize);
                        text.drawLine(arrowX, arrowY, hx1, hy1, TFT_YELLOW);
                        text.drawLine(arrowX, arrowY, hx2, hy2, TFT_YELLOW);

                        text.pushSprite(241, 10);
                        fps = 0;
                        ptime = millis();

                    }
                    
                    idx = 0;
                    inJPEG = false;
                }
            }
        } else {
            break;
        }
        }
    }

    // 受信中に複数枚完成していれば、最後に完成した1枚だけを描画する。
    drawLatestFrame();
#endif
    reportCoreDiagnostics();
}
