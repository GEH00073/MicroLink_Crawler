#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h> 
#include <esp_now.h>
#include <esp_wifi.h>

// ===== WiFi設定（secrets.ini で設定） =====
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const int wifiChannel = 3; // 送信側と同じチャンネル

// ===== ストリーミングサーバのURL =====
// AtomS3R-CAM 側で表示された IP アドレスに合わせて変更してください
// String streamURL = "http://192.168.4.1/";
String streamURL = "http://192.168.1.57/video";
// String streamURL = "http://192.168.1.125/";


// バッファサイズ
#define STREAM_BUFFER_SIZE (300 * 1024)

static LGFX_Sprite canvas(&M5.Display);   // オフスクリーン描画用バッファ
static LGFX_Sprite text(&M5.Display);   // テキスト描画用バッファ
static LGFX_Sprite lamp(&M5.Display);   // ランプ描画用バッファ

WiFiClient client;
HTTPClient http;
uint8_t* jpgBuf;
size_t jpgBufLen;

u_long ptime, ptime2;
int fps;
float angleAccum = 0; // 累積回転角（ラジアン）

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
  memcpy(&incomingData, incomingDataBuf, sizeof(incomingData));
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
    lamp.setSwapBytes(false); // スワップON(色がおかしい場合には変更する)
    lamp.setPivot(0, 0);
    lamp.setTextFont(1);
    lamp.setTextColor(0xFFFF, 0x0000);  // 白文字、黒背景
   

    // TJpg_Decoder設定
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tft_output);

    // Wi-Fi接続
    WiFi.begin(ssid, password);
    M5.Display.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        M5.Display.print(".");
    }
    M5.Display.println("\nWiFi Connected!");
    M5.Display.println(WiFi.localIP());

    // HTTP接続開始
    if (http.begin(client, streamURL)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            M5.Display.println("Connected to camera stream.");
        } else {
            M5.Display.printf("HTTP error: %d\n", httpCode);
        }
    } else {
        M5.Display.println("HTTP connect failed");
    }

    jpgBuf = (uint8_t*)malloc(STREAM_BUFFER_SIZE);
    jpgBufLen = 0;

    M5.Display.clear();

    // チャンネルを送信側と同じに固定
    esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);

    // ===== ESP-NOW 初期化 =====
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);

}

// JPEG処理用の状態管理
bool inJPEG = false;
size_t idx = 0;

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

void loop() {
    M5.update();
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

    if (client.connected() && client.available()) {
        // 一度にまとめて読み込む
        int len = client.read(jpgBuf + idx, 2048);
        if (len > 0) {
            idx += len;

            // バッファオーバーフロー防止
            if (idx >= STREAM_BUFFER_SIZE) {
                idx = 0;
                inJPEG = false;
            }

            // JPEG EOI (0xFF 0xD9) を探す
            for (int i = 1; i < len; i++) {
                if (jpgBuf[idx - i] == 0xD9 && jpgBuf[idx - i - 1] == 0xFF) {
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

                    if (incomingData.gx > 0.1 || incomingData.gx < -0.1) {
                        angleAccum += incomingData.gy * PI / 180.0f / 25.0; // gyを度からラジアンに変換して累積
                    }

                    // Text
                    if(millis() - ptime > 200){
                        text.clear();
                        text.setCursor(0,10);
                        text.setTextColor(TFT_WHITE, TFT_BLACK); 
                        text.printf("fps: %.1f \n", fps * 5.0);
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

                        M5.Display.setCursor(43, 225);
                        M5.Display.print("Auto             Serch             Zoom");
                    }

                    // Lamp
                    if(millis() - ptime2 > 500){
                        int rows = 3;
                        int cols = 5;
                        int size = 11;       // 正方形の1辺
                        int gap = 1;         // 隙間
                        int spacingX = size + gap; 
                        int spacingY = size + gap;

                        for (int r = 0; r < rows; r++) {
                        for (int c = 0; c < cols; c++) {
                            int x = c * spacingX; // 左上座標
                            int y = r * spacingY;

                            uint16_t color;
                            if (random(0, 3) == 0) {
                                color = TFT_BLACK;   // 1/3 の確率で黒
                            } else {
                                color = random(0x0001, 0xFFFF); // 残りはランダム色
                            }

                            lamp.fillRect(x, y, size, size, color);
                        }
                    }
                        lamp.pushSprite(241, 170);

                        ptime2 = millis();
                    }
                    
                    idx = 0;
                    inJPEG = false;
                    break;
                }
            }
        }
    }
}
