/*
 * MIT License
 *
 * Copyright (c) 2024 Kouhei Ito
 * Copyright (c) 2024 M5Stack
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

#include "telemetry.hpp"
#include "rc.hpp"
#include "main_loop.hpp"
#include "stampfly.hpp"

extern float rpm, set_rpm;
extern float pos, target_pos;

uint8_t Telem_mode     = 0;
uint8_t Telem_cnt      = 0;
const uint8_t MAXINDEX = 11*4+2;
const uint8_t MININDEX = 30;

void telemetry_sequence(void);
void telemetry_sequence_fast(void);
void make_telemetry_header_data(uint8_t* senddata);
void make_telemetry_data(uint8_t* senddata);
void make_telemetry_data_fast(uint8_t* senddata);
void data2log(uint8_t* data_list, float add_data, uint8_t index);
void float2byte(float x, uint8_t* dst);
void append_data(uint8_t* data, uint8_t* newdata, uint8_t index, uint8_t len);
void data_set(uint8_t* datalist, float value, uint8_t* index);
void data_set_uint32(uint8_t* datalist, uint32_t value, uint8_t* index);
void data_set_uint16(uint8_t* datalist, uint16_t value, uint8_t* index);
void data_set_uint8(uint8_t* datalist, uint8_t value, uint8_t* index);

void telemetry(void) {
    //uint8_t senddata[MAXINDEX];
    
    if (Telem_mode == 0) {
        // Send header data
        Telem_mode = 1;
        //make_telemetry_header_data(senddata);

        // Send !
        //telemetry_send(senddata, sizeof(senddata));
    } else { // if (StampFly.flag.mode > AVERAGE_MODE) {
        const uint8_t N = 10;
        // N回に一度送信
        if (Telem_cnt == 0) telemetry_sequence();
        Telem_cnt++;
        if (Telem_cnt > N - 1) Telem_cnt = 0;
        // telemetry_sequence();
    }
}

void telemetry_sequence(void) {
    uint8_t senddata[MAXINDEX];

    switch (Telem_mode) {
        case 1:
            make_telemetry_data(senddata);
            // Send !
            if (telemetry_send(&peerInfo[TELEM], senddata, sizeof(senddata)) == 1){
                //USBSerial.printf("NG Mode=%d\n\r", StampFly.flag.mode);
            }
            else{
                //USBSerial.printf("OK Mode=%d\n\r", StampFly.flag.mode);
            }
            // Telem_mode = 2;
            break;
    }
}

void make_telemetry_header_data(uint8_t* senddata) {
    float d_float;
    uint8_t d_int[4];
    uint8_t index = 0;

    index = 2;
    for (uint8_t i = 0; i < (MAXINDEX - 2) / 4; i++) {
        data2log(senddata, 0.0f, index);
        index = index + 4;
    }
    // Telemetry Header
    senddata[0] = 99;
    senddata[1] = 99;
    index       = 2;
    #if 0
    data_set(senddata, Roll_rate_kp, &index);
    #endif
}

void make_telemetry_data(uint8_t* senddata) {
    float d_float;
    uint8_t d_int[4];
    uint8_t index = 0;

    // Telemetry Header
    senddata[0] = 88;
    senddata[1] = 88;
    index       = 2;
    data_set(senddata, pos * 1.0, &index);            // 1 pos
    data_set(senddata, rpm * 1.0, &index);            // 2 speed
     }

void data_set(uint8_t* datalist, float value, uint8_t* index) {
    data2log(datalist, value, *index);
    *index = *index + 4;
}

void data_set_uint32(uint8_t* datalist, uint32_t value, uint8_t* index) {
    append_data(datalist, (uint8_t*)&value, *index, 4);
    *index = *index + 4;
}

void data_set_uint16(uint8_t* datalist, uint16_t value, uint8_t* index) {
    append_data(datalist, (uint8_t*)&value, *index, 2);
    *index = *index + 2;
}

void data_set_uint8(uint8_t* datalist, uint8_t value, uint8_t* index) {
    append_data(datalist, (uint8_t*)&value, *index, 1);
    *index = *index + 1;
}

void data2log(uint8_t* data_list, float add_data, uint8_t index) {
    uint8_t d_int[4];
    float d_float = add_data;
    float2byte(d_float, d_int);
    append_data(data_list, d_int, index, 4);
}

void float2byte(float x, uint8_t* dst) {
    uint8_t* dummy;
    dummy  = (uint8_t*)&x;
    dst[0] = dummy[0];
    dst[1] = dummy[1];
    dst[2] = dummy[2];
    dst[3] = dummy[3];
}

void append_data(uint8_t* data, uint8_t* newdata, uint8_t index, uint8_t len) {
    for (uint8_t i = index; i < index + len; i++) {
        data[i] = newdata[i - index];
    }
}
