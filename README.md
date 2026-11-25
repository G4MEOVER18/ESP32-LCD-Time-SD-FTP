<div align="center">

# ESP32-LCD-Time-SD-FTP

[![CI](https://github.com/G4MEOVER18/ESP32-LCD-Time-SD-FTP/actions/workflows/platformio-build.yml/badge.svg)](https://github.com/G4MEOVER18/ESP32-LCD-Time-SD-FTP/actions/workflows/platformio-build.yml)

**ESP32-S3 Dashboard: NTP-Uhr auf 1.47"-Display, SD-Logging, WebServer & FTP-Server.**

[![Plattform](https://img.shields.io/badge/Plattform-ESP32--S3-blue?style=flat-square)](.)
[![Build](https://img.shields.io/badge/Build-PlatformIO%20(pioarduino)-orange?style=flat-square&logo=platformio)](https://platformio.org)
[![Display](https://img.shields.io/badge/Display-ST7789%201.47%22-green?style=flat-square)](.)
[![Lizenz](https://img.shields.io/badge/Lizenz-MIT-green?style=flat-square)](LICENSE)

</div>

---

Ein ESP32-S3 holt die Uhrzeit per **NTP**, zeigt sie auf einem **1.47" ST7789-Display** (Arduino_GFX), protokolliert auf **SD-Karte** und stellt die Daten über einen **WebServer** sowie einen **FTP-Server** bereit (Dateizugriff auf die SD-Karte). Spannt bei Bedarf einen eigenen Access-Point auf.

## Features

| Bereich | Funktion |
|---------|----------|
| **Zeit** | NTP-Sync, Anzeige auf TFT |
| **Display** | 1.47" ST7789 (172×320) über Arduino_GFX |
| **SD** | Logging + Dateizugriff |
| **Netz** | STA + optionaler AP, mDNS, WebServer |
| **FTP** | SimpleFTPServer — SD-Karten-Zugriff per FTP-Client |

## Vor dem ersten Einsatz

WLAN-Zugangsdaten im Sketch (`ssid`/`password`, `ap_ssid`/`ap_password`) sind **Platzhalter** (`YOUR_…`) und müssen gesetzt werden.

## Fertige Firmware

Vorkompiliertes Image unter [**Releases**](https://github.com/G4MEOVER18/ESP32-LCD-Time-SD-FTP/releases/latest):

```bash
esptool.py --chip esp32s3 write_flash 0x0 ESP32-LCD-Time-SD-FTP_esp32s3_v1.0.0_merged.bin
```

## Selbst bauen (PlatformIO)

```bash
pio run                       # bauen
pio run -t upload             # flashen
pio device monitor -b 115200  # serielle Ausgabe
```

Aktuelle Arduino_GFX-Versionen benötigen Arduino-ESP32 **Core 3.x** (`esp32-hal-periman.h`); die genutzte `WiFi.softAP`-API ist dort rückwärtskompatibel. Daher die **pioarduino**-Plattform (gepinnt). Details: [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md).

> **Hinweis zu `versions/` und `legacy/`:** ältere Entwicklungsstände. Der gebaute Stand ist `src/ESP32S3_LCD_Time_SD/`.

## Lizenz

MIT – siehe [LICENSE](LICENSE). © 2026 Yanis Ameseder

---

## Kontakt

**Entwickler:** Yanis Ameseder · **E-Mail:** [g4me.over.18@gmail.com](mailto:g4me.over.18@gmail.com)
