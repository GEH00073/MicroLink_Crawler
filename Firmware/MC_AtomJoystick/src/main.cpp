#include <M5AtomS3.h>
#include <WiFi.h>
#include <atoms3joy.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "buzzer.h"

namespace {
constexpr uint8_t kEspNowChannel = 1;  // Must match MC_stamp_pico_crawler.
constexpr uint16_t kStickCenter = 2048;
constexpr float kStickHalfRange = 2048.0f;
constexpr float kStickDeadband = 0.05f;
constexpr uint8_t kPacketSize = 25;
constexpr uint32_t kSendIntervalMs = 50;
constexpr uint32_t kDisplayIntervalMs = 100;
constexpr float kBatteryPresentMinVoltage = 3.0f;
constexpr float kBatteryWarnVoltage = 3.7f;
constexpr float kBatteryAlarmVoltage = 3.5f;
constexpr uint32_t kBatteryAlarmIntervalMs = 10000;

const uint8_t kBroadcastAddress[6] = {0xff, 0xff, 0xff,
                                      0xff, 0xff, 0xff};

struct JoyState {
  float throttle = 0.0f;
  float aileron = 0.0f;
  float elevator = 0.0f;
  float rudder = 0.0f;
};

JoyState joy;
uint8_t sendData[kPacketSize] = {};
uint32_t lastSendMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastBatteryAlarmMs = 0;
uint32_t sentCount = 0;
uint32_t sendFailCount = 0;

float normalizeStick(uint16_t raw, bool invert) {
  float value = (static_cast<int>(raw) - kStickCenter) / kStickHalfRange;
  value = constrain(value, -1.0f, 1.0f);
  return invert ? -value : value;
}

float applyDeadband(float value) {
  return fabsf(value) < kStickDeadband ? 0.0f : value;
}

void configureJoyMapping() {
  THROTTLE = LEFTY;
  AILERON = RIGHTX;
  ELEVATOR = RIGHTY;
  RUDDER = LEFTX;
  ARM_BUTTON = LEFT_STICK_BUTTON;
  FLIP_BUTTON = RIGHT_STICK_BUTTON;
  MODE_BUTTON = RIGHT_BUTTON;
  OPTION_BUTTON = LEFT_BUTTON;
}

void readJoy() {
  joy_update();
  // Keep the existing AtomS3 joystick calibration used by BugC mode.
  joy.throttle = normalizeStick(getThrottle(), true) + 0.02f;
  joy.aileron = normalizeStick(getAileron(), true) - 0.04f;
  joy.elevator = normalizeStick(getElevator(), true) + 0.01f;
  joy.rudder = normalizeStick(getRudder(), false) - 0.05f;

  joy.aileron = applyDeadband(joy.aileron);
  joy.elevator = applyDeadband(joy.elevator);
  joy.rudder = applyDeadband(joy.rudder);
}

void copyFloatToPacket(uint8_t offset, float value) {
  memcpy(&sendData[offset], &value, sizeof(value));
}

void buildEspNowPacket() {
  // Stamp Pico accepts ff:ff:ff addressed packets, then filters by checksum.
  sendData[0] = 0xff;
  sendData[1] = 0xff;
  sendData[2] = 0xff;
  copyFloatToPacket(3, joy.rudder);
  copyFloatToPacket(7, joy.throttle);
  copyFloatToPacket(11, joy.aileron);
  copyFloatToPacket(15, joy.elevator);
  sendData[19] = getArmButton();
  sendData[20] = getFlipButton();
  sendData[21] = getModeButton();
  sendData[22] = getOptionButton();
  sendData[23] = 0;

  sendData[24] = 0;
  for (uint8_t i = 0; i < 24; ++i) {
    sendData[24] += sendData[i];
  }
}

void sendEspNowPacket() {
  buildEspNowPacket();
  const esp_err_t result =
      esp_now_send(kBroadcastAddress, sendData, sizeof(sendData));
  if (result == ESP_OK) {
    ++sentCount;
  } else {
    ++sendFailCount;
  }
}

float joyBatteryVoltage() {
  const bool battery0Valid = Battery_voltage[0] >= kBatteryPresentMinVoltage;
  const bool battery1Valid = Battery_voltage[1] >= kBatteryPresentMinVoltage;
  if (battery0Valid && battery1Valid) return min(Battery_voltage[0], Battery_voltage[1]);
  if (battery0Valid) return Battery_voltage[0];
  if (battery1Valid) return Battery_voltage[1];
  return 0.0f;
}

void drawStatus() {
  const float battery = joyBatteryVoltage();
  if (battery > 0.0f && battery <= kBatteryAlarmVoltage &&
      millis() - lastBatteryAlarmMs >= kBatteryAlarmIntervalMs) {
    lastBatteryAlarmMs = millis();
    beep();
  }

  M5.Lcd.setCursor(0, 2);
  M5.Lcd.setTextColor(
      battery > 0.0f && battery <= kBatteryAlarmVoltage ? RED :
      battery > 0.0f && battery <= kBatteryWarnVoltage ? YELLOW : GREEN,
      BLACK);
  if (battery > 0.0f) {
    M5.Lcd.printf("BAT %3.1fV   ", battery);
  } else {
    M5.Lcd.print("BAT --.-V   ");
  }
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(0, 22);
  M5.Lcd.print("BugC / Crawler");
  M5.Lcd.setCursor(0, 42);
  M5.Lcd.printf("ESP-NOW CH %d ", kEspNowChannel);
  M5.Lcd.setCursor(0, 62);
  M5.Lcd.printf("TX %lu NG %lu   ", static_cast<unsigned long>(sentCount),
                static_cast<unsigned long>(sendFailCount));
  M5.Lcd.setCursor(0, 82);
  M5.Lcd.printf("X%+.2f Y%+.2f ", joy.aileron, joy.elevator);
  M5.Lcd.setCursor(0, 102);
  M5.Lcd.printf("T%+.2f R%+.2f ", joy.throttle, joy.rudder);
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    M5.Lcd.fillScreen(RED);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.print("ESP-NOW NG");
    delay(2000);
    ESP.restart();
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, kBroadcastAddress, sizeof(kBroadcastAddress));
  peerInfo.channel = kEspNowChannel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    M5.Lcd.fillScreen(RED);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.print("PEER NG");
    delay(2000);
    ESP.restart();
  }
}
}  // namespace

void setup() {
  M5.begin();
  Wire1.begin(38, 39, 400 * 1000);
  M5.Lcd.setRotation(2);
  M5.Lcd.setTextFont(1);
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);
  setup_pwm_buzzer();
  configureJoyMapping();
  setupEspNow();
  start_tone();
}

void loop() {
  M5.update();
  readJoy();

  const uint32_t now = millis();
  if (now - lastSendMs >= kSendIntervalMs) {
    lastSendMs = now;
    sendEspNowPacket();
  }
  if (now - lastDisplayMs >= kDisplayIntervalMs) {
    lastDisplayMs = now;
    drawStatus();
  }
  delay(1);
}
