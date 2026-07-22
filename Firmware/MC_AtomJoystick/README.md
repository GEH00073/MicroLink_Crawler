# AtomJoystick Firmware

Dedicated M5AtomS3 controller firmware for the MicroLink Crawler.

## Operation

The firmware starts directly in crawler control mode. It reads the Atom
JoyStick Base and sends BugC-compatible ESP-NOW broadcast packets at 20 Hz on
Wi-Fi channel 1. There is no OTA mode, vehicle-selection menu, Toio support,
or controller-side pairing procedure.

Use it with `MC_stamp_pico_crawler`. Power on both devices and begin control.

## Build and upload

Open this directory as a PlatformIO project, select the `m5stack-atoms3`
environment, and upload over USB.
