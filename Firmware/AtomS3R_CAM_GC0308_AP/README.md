# M5StampFly Skeleton

M5Stack社が発売した StampFly と AtomJoyStick のファームウェアの骨組みを提供、あとは好きにしてね。

## 方針

- モータ、IMU、姿勢推定とToFのAPIは提供
- 制御用の400Hzの周期関数が用意されているので、そちらに独自のコードを加えることで、飛行プログラムが気軽にできるかも
- AtomJoyStickと組み合わせてテレメトリーでのログ機能を提供

## ToDo

- 要望があるならオプティカルフローや気圧などのAPIも提供
- 使い方の資料や講習会の実施

## 対応するコントローラ

- こうへい版シンプルファームウエア https://github.com/M5Fly-kanazawa/Simple_StampFly_Joy

## どこから読めばいいのか

### まずは"main_loop.cpp"から
"main_loop.cpp"の中のvoid "loop_400Hz(void)"関数から読んでみてください。ここにオリジナルコードを追加していくことになると思います。

### 次は"sensor.cpp"
センサの処理が書かれているので"sensor.cpp"も重要です。

## 参考資料

- StampFly & Atom ジョイスティック ファームウェア書き込みガイド https://docs.m5stack.com/ja/guide/hobby_kit/stampfly/stamply_firmware
- StampFlyの制御プログラムのビルドと書き込み https://rikei-tawamure.com/entry/2023/11/19/101426

 

### M5 Stamp Fly関連

- オリジナルファームウェア https://github.com/m5stack/M5StampFly
- こうへい版ファームウエア https://github.com/M5Fly-kanazawa/M5StampFly

|仕様|概要|
|----|----|
|M5StampS3|ESP32-S3@Xtensa LX7、8 MB-FLASH、Wi-Fi、OTG/CDC support|
|距離センサ|VL53L3CXV0DH/1 (0x52) @ 最大3m|
|オプティカルフローセンサ|PMW3901MB-TXQT|
|気圧センサ|BMP280（0x76）@ 300-1100 hPa|
|3軸磁力センサ|BMM150（0x10)|
|6軸IMUセンサ|BMI270|
|バッテリー|300 mAh 高電圧リチウムポリマーバッテリ（LiHV）|
|電流電圧計|INA3221AIRGVR（0x40）|
|ブザー|Built-in Buzzer @ 5020|
|Product Size|107 x 107 x 30 mm|
|Product Weight|36.2 g|

### M5 Atom JoyStick関連

- オリジナルファームウェア https://github.com/m5stack/Atom-JoyStick
- こうへい版シンプルファームウエア https://github.com/M5Fly-kanazawa/Simple_StampFly_Joy
