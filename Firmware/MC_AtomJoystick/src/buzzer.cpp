#include "buzzer.h"

#include <driver/ledc.h>

namespace {
constexpr int kBuzzerPin = 5;
constexpr int kBuzzerChannel = 0;
}

void setup_pwm_buzzer() {
  ledcSetup(kBuzzerChannel, 4000, 8);
  ledcAttachPin(kBuzzerPin, kBuzzerChannel);
}

void buzzer_sound(uint32_t frequency, uint32_t duration_ms) {
  ledcWriteTone(kBuzzerChannel, frequency);
  ledcWrite(kBuzzerChannel, 64);
  delay(duration_ms);
  ledcWriteTone(kBuzzerChannel, 0);
}

void beep() {
  buzzer_sound(4000, 80);
}

void start_tone() {
  buzzer_sound(2000, 90);
  buzzer_sound(1000, 90);
}
