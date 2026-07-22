# MicroLink Crawler

Firmware and 3D-printing data for the MicroLink Crawler project.

The project consists of a tracked crawler, an AtomS3 joystick controller, an
AtomS3R camera transmitter, and a Core2 monitor.

## Firmware

| Directory | Target hardware | Purpose |
| --- | --- | --- |
| `Firmware/MC_stamp_pico_crawler` | M5Stamp Pico | Crawler motor control and ESP-NOW control receiver. |
| `Firmware/MC_AtomJoystick` | M5AtomS3 with Atom JoyStick Base | Dedicated crawler controller. It sends BugC-compatible ESP-NOW control packets. |
| `Firmware/MC_AtomS3R_CAM_GC0308_gylo` | AtomS3R + GC0308 camera | Camera access point, MJPEG video transmitter, red-object detector, and IMU transmitter. |
| `Firmware/MC_Core2_cam_reciever_AP` | M5Stack Core2 | Camera monitor and IMU display receiver. |

Each directory is an independent PlatformIO project. Open the required
directory in PlatformIO, then build and upload it separately.

## Crawler control

1. Upload `MC_stamp_pico_crawler` to the crawler's Stamp Pico.
2. Upload `MC_AtomJoystick` to the AtomS3 joystick.
3. Power on the crawler and the joystick.

The joystick continuously broadcasts ESP-NOW control packets on Wi-Fi channel
1. No controller-side pairing menu or saved peer address is required. The
Stamp Pico accepts the broadcast packets and controls the crawler.

## Camera and monitor

1. Upload `MC_AtomS3R_CAM_GC0308_gylo` to the camera unit.
2. Upload `MC_Core2_cam_reciever_AP` to the Core2.
3. Power on the camera unit first, then the Core2.

The camera creates the following Wi-Fi access point:

| Setting | Value |
| --- | --- |
| SSID | `AtomS3R_CAM` |
| Password | `123456` |
| Wi-Fi channel | 3 |
| Camera URL | `http://192.168.4.1/video` |

The Core2 is preconfigured for this access point. An iPhone or another Wi-Fi
client can also join the access point and open the camera URL in a browser.

The Core2 disables Wi-Fi power saving while receiving the stream to avoid long
MJPEG pauses. It displays the latest decoded frame rather than waiting for old
queued frames, and also receives IMU data through ESP-NOW.

## 3D-printing data

`3D_Printer_Data/` contains the printable mechanical data for the project.

## Notes

- The controller/crawler ESP-NOW link uses channel 1. The camera/Core2 link
  uses the camera access point on channel 3. These are separate functions.
- The camera access point uses the fixed credentials above for simple setup.
  Change them in both the camera and Core2 `src/main.cpp` files if you need a
  different network name or password.
