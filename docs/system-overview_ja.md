# システム構成

[English](system-overview.md) | 日本語

MicroLink Crawlerは、30 × 30 mmのクローラー走行モジュール、着脱可能なAtomS3R-CAMモジュール、Atom JoyStickコントローラー、Core2モニター、およびシステム全体を収納するブックサイズの携帯ケースで構成されます。

走行制御とカメラ・モニターの機能は、それぞれの役割を分けるため別のWi-Fiチャンネルを使用します。

| 経路 | デバイス | 機能 |
| --- | --- | --- |
| 走行制御 | Atom JoyStick → M5Stamp Pico | チャンネル1のESP-NOWによる走行制御 |
| 映像 | AtomS3R-CAM → Core2 | チャンネル3のカメラアクセスポイント経由MJPEG |
| テレメトリー | AtomS3R-CAM → Core2 | チャンネル3のESP-NOWによるIMU情報 |

Stamp Picoは2台のギヤードモーターとライトを制御します。カメラモジュールは走行モジュールから取り外せ、Core2は遠隔で映像と姿勢情報を表示します。

システム図は[README](../README_ja.md)、プロジェクトの配置は[ファームウェア構成](firmware-overview_ja.md)を参照してください。
