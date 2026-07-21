# MicroLink Crawler

コンテスト応募用の MicroLink Crawler リポジトリです。

## 構成

- `Firmware/MC_Core2_cam_reciever_AP/` — Core2 受信機ファームウェア
- `Firmware/MC_AtomS3R_CAM_GC0308_gylo/` — AtomS3R カメラ送信機ファームウェア
- `Firmware/MC_stamp_pico_crawler/` — Stamp Pico Crawler ファームウェア
- `3D_Printer_Data/` — 3D プリンタ用データ

## Wi-Fi 設定

各ファームウェアの `secrets.ini.example` を `secrets.ini` にコピーし、SSID とパスワードを設定してください。`secrets.ini` は Git 管理対象外のため、認証情報は公開されません。

各ファームウェアは PlatformIO プロジェクトです。対象ディレクトリを個別に PlatformIO で開いてビルド・書き込みしてください。
