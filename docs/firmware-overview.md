# Firmware overview

English | [日本語](firmware-overview_ja.md)

The repository contains four independent PlatformIO projects. Build and upload each project from its own directory.

| Project | Hardware | Main role | Communication partner |
| --- | --- | --- | --- |
| [`Firmware/MC_stamp_pico_crawler`](../Firmware/MC_stamp_pico_crawler/) | M5Stamp Pico | Receives BugC-compatible ESP-NOW control packets on channel 1; controls two motors and lights | `MC_AtomJoystick` |
| [`Firmware/MC_AtomJoystick`](../Firmware/MC_AtomJoystick/) | AtomS3 with Atom JoyStick Base | Sends BugC-compatible ESP-NOW control packets on channel 1 | `MC_stamp_pico_crawler` |
| [`Firmware/MC_AtomS3R_CAM_GC0308_gylo`](../Firmware/MC_AtomS3R_CAM_GC0308_gylo/) | AtomS3R with GC0308 camera | Camera access point, MJPEG FPV, IMU transmission, and red-object detection on channel 3 | `MC_Core2_cam_reciever_AP` |
| [`Firmware/MC_Core2_cam_reciever_AP`](../Firmware/MC_Core2_cam_reciever_AP/) | M5Stack Core2 | Receives and displays MJPEG video and IMU information | `MC_AtomS3R_CAM_GC0308_gylo` |

The control and camera systems are separate: do not confuse the crawler-control channel 1 with the camera/Core2 channel 3. Consult each project’s existing README and `platformio.ini` for its build configuration.
