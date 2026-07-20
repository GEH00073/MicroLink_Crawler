#include <M5Unified.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>

#ifndef WIFI_SSID
#define WIFI_SSID "B501"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "30023002"
#endif

#ifndef VIDEO_FEED_URL
#define VIDEO_FEED_URL "http://192.168.1.199/video_feed"
#endif

constexpr int PIN_SCL = 9;
constexpr int PIN_SDA = 7;
constexpr int PIN_RST = 5;
constexpr int PIN_DC  = 3;
constexpr int PIN_BL  = 1;
constexpr int PIN_CS  = -1;
constexpr int DISPLAY_WIDTH = 240;
constexpr int DISPLAY_HEIGHT = 240;
constexpr int PANEL_OFFSET_Y = 0;

class LGFX_StampS3_SPI_ST7789 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;
  lgfx::Light_PWM    _light_instance;

public:
  LGFX_StampS3_SPI_ST7789() {
    auto cfg = _bus_instance.config();
    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 3;
    cfg.freq_write = 40000000;
    cfg.freq_read  = 16000000;
    cfg.pin_sclk   = PIN_SCL;
    cfg.pin_mosi   = PIN_SDA;
    cfg.pin_miso   = -1;
    cfg.pin_dc     = PIN_DC;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto panel_cfg = _panel_instance.config();
    panel_cfg.pin_cs      = PIN_CS;
    panel_cfg.pin_rst     = PIN_RST;
    panel_cfg.panel_width  = DISPLAY_WIDTH;
    panel_cfg.panel_height = DISPLAY_HEIGHT;
    panel_cfg.memory_width = 240;
    panel_cfg.memory_height = 240;
    panel_cfg.offset_x     = 0;
    panel_cfg.offset_y     = PANEL_OFFSET_Y;
    panel_cfg.invert       = true;
    _panel_instance.config(panel_cfg);

    auto light_cfg = _light_instance.config();
    light_cfg.pin_bl = PIN_BL;
    _light_instance.config(light_cfg);
    _panel_instance.setLight(&_light_instance);

    setPanel(&_panel_instance);
  }
};

namespace {
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kReconnectDelayMs = 2000;
constexpr uint16_t kStreamReadTimeout = 5000;
constexpr size_t kMaxJpegFrameBytes = 96 * 1024;
constexpr size_t kStreamReadChunkBytes = 2048;
constexpr size_t kMaxZoomSpritePixels = 30000;

LGFX_StampS3_SPI_ST7789 ExtDisplay;
LGFX_Sprite frameSprite(&ExtDisplay);
HTTPClient http;
WiFiClient client;
uint8_t *jpegBuffer = nullptr;
size_t jpegLength = 0;
uint32_t lastReconnectAttemptMs = 0;
bool inJpeg = false;
bool drawingToSprite = false;
uint16_t frameSpriteWidth = 0;
uint16_t frameSpriteHeight = 0;

bool drawJpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
    if (drawingToSprite) {
        if (y >= frameSprite.height() || y + h <= 0 || x >= frameSprite.width() || x + w <= 0) {
            return true;
        }
        frameSprite.pushImage(x, y, w, h, bitmap);
        return true;
    }

    if (y >= ExtDisplay.height() || y + h <= 0 || x >= ExtDisplay.width() || x + w <= 0) {
        return true;
    }
    ExtDisplay.pushImage(x, y, w, h, bitmap);
    return true;
}

bool ensureFrameSprite(uint16_t width, uint16_t height)
{
    if (static_cast<size_t>(width) * height > kMaxZoomSpritePixels) {
        return false;
    }

    if (frameSpriteWidth == width && frameSpriteHeight == height) {
        return true;
    }

    frameSprite.deleteSprite();
    frameSprite.setColorDepth(16);
    frameSprite.setSwapBytes(false);
    if (!frameSprite.createSprite(width, height)) {
        frameSpriteWidth = 0;
        frameSpriteHeight = 0;
        return false;
    }

    frameSpriteWidth = width;
    frameSpriteHeight = height;
    frameSprite.setPivot(width / 2, height / 2);
    return true;
}

void drawMessage(const char *line1, const char *line2 = nullptr)
{
    ExtDisplay.fillScreen(TFT_BLACK);
    ExtDisplay.setTextColor(TFT_WHITE, TFT_BLACK);
    ExtDisplay.setTextDatum(middle_center);
    ExtDisplay.drawString(line1, ExtDisplay.width() / 2, ExtDisplay.height() / 2 - 12);
    if (line2 != nullptr) {
        ExtDisplay.drawString(line2, ExtDisplay.width() / 2, ExtDisplay.height() / 2 + 12);
    }
}

void connectWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    drawMessage("WiFi connecting", WIFI_SSID);
    Serial.printf("Connecting to WiFi SSID: %s\r\n", WIFI_SSID);

    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < kWifiTimeoutMs) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi connected. IP: %s\r\n", WiFi.localIP().toString().c_str());
        drawMessage("WiFi connected", WiFi.localIP().toString().c_str());
        delay(700);
    } else {
        Serial.println("WiFi connection failed.");
        drawMessage("WiFi failed", "check SSID/PASS");
    }
}

bool connectStream()
{
    http.end();

    Serial.printf("Opening stream: %s\r\n", VIDEO_FEED_URL);
    http.setTimeout(kStreamReadTimeout);
    http.setReuse(false);

    if (!http.begin(client, VIDEO_FEED_URL)) {
        Serial.println("HTTP begin failed.");
        drawMessage("HTTP begin failed");
        return false;
    }

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        Serial.printf("HTTP GET failed: %d\r\n", statusCode);
        http.end();
        drawMessage("HTTP GET failed", String(statusCode).c_str());
        return false;
    }

    jpegLength = 0;
    inJpeg = false;
    Serial.println("Video stream connected.");
    ExtDisplay.fillScreen(TFT_BLACK);
    return true;
}

void closeStream()
{
    http.end();
    jpegLength = 0;
    inJpeg = false;
}

bool decodeCurrentFrame()
{
    uint16_t width = 0;
    uint16_t height = 0;
    if (TJpgDec.getJpgSize(&width, &height, jpegBuffer, jpegLength) != JDR_OK || width == 0 || height == 0) {
        Serial.printf("JPEG size read failed: %u bytes\r\n", static_cast<unsigned>(jpegLength));
        return false;
    }

    const uint16_t displayWidth = ExtDisplay.width();
    const uint16_t displayHeight = ExtDisplay.height();
    const float coverZoom = max(static_cast<float>(displayWidth) / width,
                                static_cast<float>(displayHeight) / height);

    if (coverZoom > 1.0f && ensureFrameSprite(width, height)) {
        frameSprite.fillScreen(TFT_BLACK);
        TJpgDec.setJpgScale(1);
        drawingToSprite = true;
        const JRESULT result = TJpgDec.drawJpg(0, 0, jpegBuffer, jpegLength);
        drawingToSprite = false;

        if (result != JDR_OK) {
            Serial.printf("JPEG decode failed: %d bytes=%u\r\n", result, static_cast<unsigned>(jpegLength));
            return false;
        }

        ExtDisplay.setPivot(displayWidth / 2, displayHeight / 2);
        ExtDisplay.startWrite();
        frameSprite.pushRotateZoom(&ExtDisplay, displayWidth / 2, displayHeight / 2, 0, coverZoom, coverZoom);
        ExtDisplay.endWrite();
        return true;
    }

    const uint16_t imageLongEdge = max(width, height);
    const uint16_t displayLongEdge = max(displayWidth, displayHeight);
    uint8_t scale = 1;
    while (scale < 8 && imageLongEdge / (scale << 1) >= displayLongEdge) {
        scale <<= 1;
    }

    const uint16_t scaledWidth = width / scale;
    const uint16_t scaledHeight = height / scale;
    const int16_t x = (displayWidth - scaledWidth) / 2;
    const int16_t y = (displayHeight - scaledHeight) / 2;

    TJpgDec.setJpgScale(scale);
    ExtDisplay.startWrite();
    const JRESULT result = TJpgDec.drawJpg(x, y, jpegBuffer, jpegLength);
    ExtDisplay.endWrite();
    if (result != JDR_OK) {
        Serial.printf("JPEG decode failed: %d bytes=%u\r\n", result, static_cast<unsigned>(jpegLength));
        return false;
    }
    return true;
}

void readAndDrawFrames()
{
    while (client.connected() && WiFi.status() == WL_CONNECTED) {
        M5.update();

        if (!client.available()) {
            delay(1);
            continue;
        }

        const size_t freeBytes = kMaxJpegFrameBytes - jpegLength;
        if (freeBytes == 0) {
            Serial.println("JPEG frame too large; dropping frame.");
            inJpeg = false;
            jpegLength = 0;
            continue;
        }

        const size_t requestLength = min(kStreamReadChunkBytes, freeBytes);
        const int readLength = client.read(jpegBuffer + jpegLength, requestLength);
        if (readLength <= 0) {
            delay(1);
            continue;
        }
        jpegLength += static_cast<size_t>(readLength);

        size_t frameStart = 0;
        if (!inJpeg) {
            bool foundStart = false;
            for (size_t i = 0; i + 1 < jpegLength; ++i) {
                if (jpegBuffer[i] == 0xFF && jpegBuffer[i + 1] == 0xD8) {
                    frameStart = i;
                    foundStart = true;
                    inJpeg = true;
                    break;
                }
            }

            if (!foundStart) {
                if (jpegLength > 1) {
                    jpegBuffer[0] = jpegBuffer[jpegLength - 1];
                    jpegLength = 1;
                }
                continue;
            }

            if (frameStart > 0) {
                memmove(jpegBuffer, jpegBuffer + frameStart, jpegLength - frameStart);
                jpegLength -= frameStart;
            }
        }

        for (size_t i = 1; i < jpegLength; ++i) {
            if (jpegBuffer[i - 1] == 0xFF && jpegBuffer[i] == 0xD9) {
                const size_t frameLength = i + 1;
                const size_t remainingLength = jpegLength - frameLength;

                jpegLength = frameLength;
                decodeCurrentFrame();

                if (remainingLength > 0) {
                    memmove(jpegBuffer, jpegBuffer + frameLength, remainingLength);
                }
                jpegLength = remainingLength;
                inJpeg = false;
                break;
            }
        }
    }
}
}  // namespace

void setup()
{
    M5.begin();
    Serial.begin(115200);
    delay(200);

    ExtDisplay.init();
    ExtDisplay.setRotation(1);
    ExtDisplay.setBrightness(200);
    ExtDisplay.clear(TFT_BLACK);

    jpegBuffer = static_cast<uint8_t *>(malloc(kMaxJpegFrameBytes));
    if (jpegBuffer == nullptr) {
        drawMessage("No JPEG buffer");
        Serial.println("Failed to allocate JPEG frame buffer.");
        while (true) {
            delay(1000);
        }
    }

    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(drawJpegOutput);
    connectWifi();
}

void loop()
{
    M5.update();

    if (WiFi.status() != WL_CONNECTED) {
        closeStream();
        connectWifi();
        delay(kReconnectDelayMs);
        return;
    }

    if (!client.connected()) {
        if (millis() - lastReconnectAttemptMs < kReconnectDelayMs) {
            delay(10);
            return;
        }
        lastReconnectAttemptMs = millis();
        drawMessage("Opening stream", VIDEO_FEED_URL);
        if (!connectStream()) {
            delay(kReconnectDelayMs);
            return;
        }
    }

    readAndDrawFrames();
    closeStream();
    drawMessage("Stream lost", "reconnecting");
}
