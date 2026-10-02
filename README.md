# FujiNetMicro

Network-adapter firmware for the RS-232 FujiNet boards, built on ESP32/ESP32-S3 hardware.

FujiNetMicro is a hard fork of the [FujiNet firmware](https://github.com/FujiNetWIFI/fujinet-firmware),
reduced to the RS-232 boards only.

## Supported boards

| Board | MCU | Notes |
|---|---|---|
| `fujinet-rs232-rev0` | ESP32 (8 MB) | Original RS-232 board |
| `fujinet-rs232-s3` | ESP32-S3 | |
| `fujinet-rs232-s3-palm` | ESP32-S3 | Palm HotSync cradle defaults, no hardware flow control |
| `fujiversal-rs232` | ESP32-S3 | Fujiversal board, USB CDC host |

There is also a host build (FujiNet-PC) for the RS232 target: `./build.sh -p RS232`.

## Generating platformio configuration for your board

See [build.sh documentation](build-sh.md) for full documentation on using build.sh to configure your platformio ini files.

## Upstream

FujiNet documentation lives on the [FujiNet wiki](https://github.com/FujiNetWIFI/fujinet-platformio/wiki)
and at https://fujinet.online/. FujiNet discussion happens on Discord: https://discord.gg/7MfFTvD
