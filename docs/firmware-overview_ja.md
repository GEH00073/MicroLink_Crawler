# ファームウェア構成

[English](firmware-overview.md) | 日本語

リポジトリには4つの独立したPlatformIOプロジェクトがあります。各プロジェクトはそれぞれのディレクトリからビルド・書き込みを行います。

| プロジェクト | ハードウェア | 主な役割 | 通信相手 |
| --- | --- | --- | --- |
| [`Firmware/MC_stamp_pico_crawler`](../Firmware/MC_stamp_pico_crawler/) | M5Stamp Pico | チャンネル1でBugC互換ESP-NOW制御パケットを受信し、2台のモーターとライトを制御 | `MC_AtomJoystick` |
| [`Firmware/MC_AtomJoystick`](../Firmware/MC_AtomJoystick/) | AtomS3 + Atom JoyStick Base | チャンネル1でBugC互換ESP-NOW制御パケットを送信 | `MC_stamp_pico_crawler` |
| [`Firmware/MC_AtomS3R_CAM_GC0308_gylo`](../Firmware/MC_AtomS3R_CAM_GC0308_gylo/) | AtomS3R + GC0308カメラ | チャンネル3のカメラアクセスポイント、MJPEG FPV、IMU送信、赤色物体検出 | `MC_Core2_cam_reciever_AP` |
| [`Firmware/MC_Core2_cam_reciever_AP`](../Firmware/MC_Core2_cam_reciever_AP/) | M5Stack Core2 | MJPEG映像とIMU情報を受信・表示 | `MC_AtomS3R_CAM_GC0308_gylo` |

走行制御系とカメラ系は別です。走行制御のチャンネル1と、カメラ／Core2のチャンネル3を混同しないでください。ビルド設定は各プロジェクトの既存READMEおよび`platformio.ini`を参照してください。
