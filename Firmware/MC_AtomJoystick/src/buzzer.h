#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

void setup_pwm_buzzer();
void beep();
void start_tone();
void buzzer_sound(uint32_t frequency, uint32_t duration_ms);

#endif
