# Vision and telemetry

English | [日本語](vision-and-telemetry_ja.md)

The detachable AtomS3R-CAM module creates a Wi-Fi access point on channel 3 and serves an MJPEG stream at `/video`. Core2 connects to that access point and displays the video.

The camera firmware sends IMU data through ESP-NOW on channel 3 at 10 Hz. Core2 uses that information to display heading and tilt. The camera also performs red-object detection; when a red object is detected, the system supplies a green detection box and its centre coordinates to the monitor.

Two implementation choices reduce visible latency or long pauses:

- Core2 disables Wi-Fi power saving while streaming and discards older frames when necessary so the latest decoded frame is preferred.
- The camera uses a 350 ms TCP send timeout, limiting how long a slow client can hold up streaming work.

For settings and build targets, see the [camera firmware README](../Firmware/MC_AtomS3R_CAM_GC0308_gylo/README.md) and [Core2 firmware README](../Firmware/MC_Core2_cam_reciever_AP/README.md).
