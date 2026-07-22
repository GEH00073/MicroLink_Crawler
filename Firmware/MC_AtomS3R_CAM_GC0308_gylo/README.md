# AtomS3R Camera Transmitter

PlatformIO firmware for an AtomS3R with a GC0308 camera.

## Features

- Creates a Wi-Fi access point and serves an MJPEG stream at `/video`.
- Sends IMU data through ESP-NOW at 10 Hz.
- Detects red objects and draws a 2-pixel green bounding box at 10 Hz.
- Uses a bounded TCP send timeout so a slow client cannot stop camera capture
  or IMU transmission for several seconds.

## Network settings

| Setting | Value |
| --- | --- |
| SSID | `AtomS3R_CAM` |
| Password | `123456` |
| Wi-Fi channel | 3 |
| Access point address | `192.168.4.1` |
| Stream URL | `http://192.168.4.1/video` |

The Core2 monitor firmware is configured to use these same settings. Change
the `AP_SSID` and `AP_PASSWORD` constants in `src/main.cpp`, and make the
matching change in the Core2 firmware, if different credentials are required.

## Build and upload

Open this directory as a PlatformIO project and use the `m5stack-atoms3r`
environment. Upload over USB.
