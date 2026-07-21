/*
 * MIT License
 *
 * Copyright (c) 2024 Kouhei Ito
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "unit_rolleri2c.hpp"
#include <M5Unified.h>
#include <Arduino.h>
#include <FastLED.h>
#include "stampfly.hpp"
#include "main_loop.hpp"

UnitRollerI2C RollerI2C;  // Create a UNIT_ROLLERI2C object
uint32_t p, i, d;         // Defines a variable to store the PID value
uint8_t r, g, b;

void setup() {
    M5.begin();

    pinMode (18, OUTPUT);
    digitalWrite(18, 0);
    pinMode (18, OUTPUT);
    pinMode (19, OUTPUT);
    pinMode (21, OUTPUT);
    pinMode (22, OUTPUT);
    pinMode (32, OUTPUT);
    pinMode (33, OUTPUT);
    digitalWrite(18, false);
    digitalWrite(19, false);
    digitalWrite(21, false);
    digitalWrite(22, false);
    digitalWrite(32, LOW);
    digitalWrite(33, LOW);

    ledcSetup(0, 1000, 8);
    ledcAttachPin(18, 0);
    ledcSetup(1, 1000, 8);
    ledcAttachPin(19, 1);
    ledcSetup(2, 1000, 8);
    ledcAttachPin(21, 2);
    ledcSetup(3, 1000, 8);
    ledcAttachPin(22, 3);

    delay(2000);

    init_copter();
    delay(100);
}

void loop() {
    // RollerI2C.setSpeed(1000);
    // RollerI2C.setOutput(1);
    // delay(2000);
    // RollerI2C.setOutput(0);
    // delay(2000);
  
    // printf("speed: %d %d\n", RollerI2C.getSpeedReadback()/100, RollerI2C.getPosReadback()/100 % 360);
    
    loop_400Hz();
}
