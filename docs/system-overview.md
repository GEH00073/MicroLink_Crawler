# System overview

English | [日本語](system-overview_ja.md)

MicroLink Crawler combines a 30 × 30 mm crawler drive module, a detachable AtomS3R-CAM module, an Atom JoyStick controller, a Core2 monitor, and a book-sized carrying case for the complete system.

The drive-control and camera-monitor functions use separate Wi-Fi channels so their roles remain distinct:

| Path | Devices | Function |
| --- | --- | --- |
| Control | Atom JoyStick → M5Stamp Pico | ESP-NOW crawler control on channel 1 |
| Video | AtomS3R-CAM → Core2 | MJPEG through the camera access point on channel 3 |
| Telemetry | AtomS3R-CAM → Core2 | IMU data through ESP-NOW on channel 3 |

The Stamp Pico controls the two geared motors and lights. The camera module can be removed from the drive module; Core2 displays video and orientation information remotely.

See the [README](../README.md) for the system diagram and [firmware overview](firmware-overview.md) for project locations.
