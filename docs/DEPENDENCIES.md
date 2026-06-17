# Abhängigkeiten

Reproduzierbarer Build über **PlatformIO** mit der **pioarduino**-Plattform
(Arduino-ESP32 Core 3.1.x). Versionen in `platformio.ini` gepinnt.

| Abhängigkeit | Version | Zweck |
|--------------|---------|-------|
| Plattform `pioarduino` | `53.03.13` | Arduino-ESP32 Core 3.1.x |
| `moononournation/GFX Library for Arduino` | `^1.4.9` | Display (ST7789 1.47") |
| `xreef/SimpleFTPServer` | `^2.1.7` | FTP-Server (SD-Zugriff) |

`WiFi`, `WebServer`, `ESPmDNS`, `SD`, `FS`, `time` sind Teil des ESP32-Cores.

## Warum pioarduino (Core 3.x)?

Aktuelle Arduino_GFX-Versionen inkludieren `esp32-hal-periman.h`, das es erst ab
Arduino-ESP32 **Core 3.0** gibt. Das offizielle PlatformIO liefert nur Core 2.x,
womit der Build mit `fatal error: esp32-hal-periman.h` abbricht. Die im Sketch
genutzte `WiFi.softAP`-API ist in Core 3.x rückwärtskompatibel.

## Konfiguration

WLAN-Zugangsdaten (`ssid`/`password`, `ap_ssid`/`ap_password`) und Display-Pins
sind im Sketch definiert. Die Credentials sind Platzhalter (`YOUR_…`) und müssen
gesetzt werden.
