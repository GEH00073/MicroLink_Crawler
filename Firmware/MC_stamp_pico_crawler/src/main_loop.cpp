/*
 * MIT License
 *
 * Copyright (c) 2024 Kouhei Ito
 * Copyright (c) 2024 M5Stack
 * Copyright (c) 2025 Jun Kataoka
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

//
// MicroLink Crawler Control Main Module
//
// Desigend by Kouhei Ito 2023~2024
//
// 2024-08-11 Control skeleton created

#include "main_loop.hpp"
#include "rc.hpp"
#include "telemetry.hpp"
#include "crawler_state.hpp"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

float rpm, set_rpm;
float pos, target_pos;
bool head_light;

void IRAM_ATTR onTimer(void);
void init_crawler(void);
void update_loop400Hz(void);
void init_mode(void);
void average_mode(void);
void flight_mode(void);
void parking_mode(void);
void loop_400Hz(void);

// Main loop
void loop_400Hz(void) {
    // 400Hzで以降のコードが実行

    update_loop400Hz();
    rc_pairing_task();

    //****************************************** */

    // 前後進だけを反転し、左右回転（RUDDER）の向きは維持する。
    float left  = -Stick[ELEVATOR] + Stick[RUDDER];
    float right = -Stick[ELEVATOR] - Stick[RUDDER];

    if (left > 1.0) left = 1.0;
    if (left < -1.0) left = -1.0;
    if (right > 1.0) right = 1.0;
    if (right < -1.0) right = -1.0;

    if (left > 0) {
        ledcWrite(1, int(left * 248));  // 前進
        ledcWrite(0, 0);
    } else {
        ledcWrite(1, 0);
        ledcWrite(0, int(-left * 248)); // 後退
    }

    if (right > 0) {
        ledcWrite(2, int(right * 254)); // 前進
        ledcWrite(3, 0);
    } else {
        ledcWrite(2, 0);
        ledcWrite(3, int(-right * 254)); // 後退
    }

    if(Stick[CONTROLMODE] > 0.5){
        head_light = !head_light;
        delay(500);
    }
    if(head_light == true){
        digitalWrite(32, HIGH);
    }else{
        digitalWrite(32, LOW);
    }


    // Serial.printf("cont: %3.2f alt :%3.2f \n", Stick[CONTROLMODE], Stick[ALTCONTROLMODE]);
    Serial.printf("head_light %d \n", head_light);

    //****************************************** */

    //// Telemetry
    telemetry();
    CrawlerState.flag.oldmode = CrawlerState.flag.mode;  // Memory now mode
    
    // End of Loop_400Hz function    
}

// 割り込み関数
// Intrupt function
hw_timer_t* timer = NULL;
void IRAM_ATTR onTimer(void) {
    CrawlerState.flag.loop = 1;
    //loop_400Hz();
}

// Initialize the crawler controller.
void init_crawler(void) {
    //disableCore1WDT();
    // Initialize Mode
    CrawlerState.flag.mode = INIT_MODE;
    CrawlerState.flag.loop = 0;
    // Initialize Serial communication
    Serial.begin(115200);
    // Serial2.begin(115200, SERIAL_8N1, 32, 33);
    delay(1500);
    Serial.printf("Start MicroLink Crawler\r\n");
    // motor_init();
    // sensor_init();
    rc_init();

    // init button G0
    // init_button();
    // setup_pwm_buzzer();
    Serial.printf("Crawler initialization complete\r\n");
    // start_tone();

    // 割り込み設定
    // Initialize intrupt
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, 2500, true);
    timerAlarmEnable(timer);

}

//loop400Hzの更新関数
void update_loop400Hz(void) {
    uint32_t now_time;

    while (CrawlerState.flag.loop == 0);
    CrawlerState.flag.loop = 0;

    #if 0
    Serial.printf("%9.4f %9.4f %04d\n\r", 
        CrawlerState.times.elapsed_time,
        CrawlerState.times.interval_time,
        CrawlerState.sensor.bottom_tof_range);
    #endif

    //Clock
    now_time = micros();
    CrawlerState.times.old_elapsed_time = CrawlerState.times.elapsed_time;
    CrawlerState.times.elapsed_time = 1e-6 * (now_time - CrawlerState.times.start_time);
    CrawlerState.times.interval_time = CrawlerState.times.elapsed_time - CrawlerState.times.old_elapsed_time;
    
    // Read Sensor Value
    // sensor_read(&CrawlerState.sensor);
    
    // LED Drive
    // led_drive();
}

void init_mode(void) {
    // motor_stop();
    CrawlerState.counter.offset = 0;
    //Mode change
    CrawlerState.flag.mode = AVERAGE_MODE;
    return;

}

void average_mode(void) {
    // Gyro offset Estimate 角速度のオフセットを取得
    if (CrawlerState.counter.offset < AVERAGENUM) {
        // sensor_calc_offset_avarage();
        CrawlerState.counter.offset++;
        return;
    }
    // Mode change
    CrawlerState.flag.mode   = PARKING_MODE;
    CrawlerState.times.start_time = micros();
    CrawlerState.times.old_elapsed_time = 0.0f;
    return;
}

void flight_mode(void) {
    //飛行するためのコードを以下に記述する
}

void parking_mode(void) {
    //着陸している時に行う処理を記述する
}
