# Core2 Camera Monitor

PlatformIO firmware for an M5Stack Core2 used as the MicroLink Crawler camera
monitor.

## Operation

The monitor connects to the AtomS3R camera access point and displays the MJPEG
stream from `http://192.168.4.1/video`. It also receives and displays IMU data
from the camera through ESP-NOW.

The expected camera network is:

| Setting | Value |
| --- | --- |
| SSID | `AtomS3R_CAM` |
| Password | `123456` |

Wi-Fi power saving is disabled while streaming. The receiver discards old
frames when necessary so the display follows the most recent decoded frame.

## Build and upload

Open this directory as a PlatformIO project, select the `m5stack-core2`
environment, and upload over USB.
