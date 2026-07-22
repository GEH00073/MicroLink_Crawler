# Stamp Pico Crawler Firmware

PlatformIO firmware for the M5Stamp Pico installed in the MicroLink Crawler.

## Control link

The crawler receives 25-byte BugC-compatible control packets through ESP-NOW
on Wi-Fi channel 1. `MC_AtomJoystick` sends these packets as broadcast frames,
so the joystick can control the crawler immediately after both devices are
powered on. No controller-side pairing menu or saved peer address is needed.

The packet carries rudder, throttle, aileron, elevator, four button states,
and a checksum. The crawler validates the packet length, destination, and
checksum before applying it.

## Build and upload

Open this directory as a PlatformIO project and upload it to the Stamp Pico
over USB.
