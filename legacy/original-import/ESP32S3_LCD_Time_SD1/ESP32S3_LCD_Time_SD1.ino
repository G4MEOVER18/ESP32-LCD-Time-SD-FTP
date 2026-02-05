#include <WiFi.h>
#include <time.h>

#include "FS.h"
#include "SD_MMC.h"
#include <Arduino_GFX_Library.h>
#include <WebServer.h>

// -------------------------------
// Farben (RGB565)
// -------------------------------
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFD20

// -------------------------------
// WLAN + Zeit
// -------------------------------
const char* ssid     = "Galaxy S21 FE 5G 1cu8";
const char* password = "12345678";

long timezone = 1;      // UTC+1
byte daysavetime = 1;   // Sommerzeit

bool wifiConnected = false;
bool timeValid     = false;

// Text leicht nach rechts (runde Ecken)
const int MARGIN_X = 8;

// -------------------------------
// Display-Pins (Waveshare ESP32-S3-LCD-1.47)
// -------------------------------
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS   42
#define TFT_DC   41
#define TFT_RST  39
#define TFT_BL   48

// Panel 240x320 Version
#define PANEL_WIDTH  240
#define PANEL_HEIGHT 320

// BOOT-Taster als Scroll-Button (GPIO0 beim S3)
#define SCROLL_BUTTON 0

// -------------------------------
// Arduino_GFX Setup
// -------------------------------
Arduino_ESP32SPI *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED
);

// rotation = 3 → Querformat
Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  TFT_RST,
  3,         // rotation
  true,      // IPS
  PANEL_WIDTH,
  PANEL_HEIGHT,
  0, 0,
  0, 0
);

// -------------------------------
// SD-Dateiliste
// -------------------------------
String fileLines[64];   // mehr Einträge möglich
int fileCount = 0;
int scrollIndex = 0;    // Startindex für Scrollen
bool lastButtonState = HIGH;

// -------------------------------
// Webserver (SD als Netzwerk-Speicher via Browser)
// -------------------------------
WebServer server(80);

// Hilfsfunktion: Content-Type nach Endung
String getContentType(const String& path) {
  if (path.endsWith(".htm") || path.endsWith(".html")) return "text/html";
  if (path.endsWith(".txt")) return "text/plain";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js"))  return "application/javascript";
  return "application/octet-stream";
}

// -------------------------------
// SD Liste einlesen
// -------------------------------
void buildFileList() {
  fileCount = 0;

  File root = SD_MMC.open("/");
  if (!root) {
    fileLines[fileCount++] = "SD open failed";
    return;
  }

  while (fileCount < 64) {
    File file = root.openNextFile();
    if (!file) break;

    String line;
    if (file.isDirectory()) {
      line = "[D] ";
      line += file.name();
    } else {
      line  = "    ";
      line += file.name();
      line += " (";
      line += String(file.size());
      line += "B)";
    }

    if (line.length() > 30)
      line = line.substring(0, 29) + ">";

    fileLines[fileCount++] = line;
    file.close();
  }

  if (fileCount == 0)
    fileLines[fileCount++] = "SD empty";

  scrollIndex = 0;
}

// -------------------------------
// System-Monitor-Werte
// -------------------------------
void getSystemInfo(size_t &freeHeap, uint32_t &freqMHz, int &rssi) {
  freeHeap = ESP.getFreeHeap();
  freqMHz  = getCpuFrequencyMhz();
  rssi     = (wifiConnected ? WiFi.RSSI() : -100);
}

// -------------------------------
// UI: WiFi-Signal als Balken (0–4)
// -------------------------------
int wifiBarsFromRSSI(int rssi) {
  if (!wifiConnected) return 0;
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

void drawWifiIcon(int x, int y, int bars) {
  // einfache Balkengrafik
  int w = 5;
  int h = 4;
  for (int i = 0; i < 4; i++) {
    uint16_t color = (i < bars) ? GREEN : BLUE;
    gfx->fillRect(x + i*(w+1), y + (3-i)*h, w, h, color);
  }
}

// -------------------------------
// UI: Akku-Symbol (Platzhalter 100%)
// -------------------------------
int batteryPercent = 100; // später über ADC messbar

void drawBatteryIcon(int x, int y, int percent) {
  // Rahmen
  gfx->drawRect(x, y, 24, 10, WHITE);
  gfx->fillRect(x+24, y+2, 2, 6, WHITE); // „Nupsie“

  int innerWidth = 24 - 2;
  int filled = (innerWidth * percent) / 100;
  gfx->fillRect(x+1, y+1, filled, 8, (percent > 20 ? GREEN : RED));
}

// -------------------------------
// Obere Info-Leiste (Uhr + WLAN + Icons)
// -------------------------------
void drawTopBar() {
  gfx->fillRect(0, 0, PANEL_WIDTH, 80, BLACK);

  // Uhr
  struct tm timeinfo;
  bool got = getLocalTime(&timeinfo, 150);

  gfx->setTextSize(3);
  gfx->setCursor(MARGIN_X, 5);

  if (!got) {
    gfx->setTextColor(RED, BLACK);
    gfx->print("NO TIME");
  } else {
    timeValid = true;
    gfx->setTextColor(WHITE, BLACK);

    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    gfx->print(buf);

    gfx->setTextSize(1);
    gfx->setCursor(MARGIN_X, 40);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);
    gfx->print(buf);
  }

  // WLAN Text
  gfx->setTextSize(1);
  gfx->setCursor(MARGIN_X, 60);

  if (wifiConnected) {
    gfx->setTextColor(GREEN, BLACK);
    gfx->print("WiFi OK ");
  } else {
    gfx->setTextColor(RED, BLACK);
    gfx->print("WiFi FAIL ");
  }
  gfx->print(ssid);

  // Systeminfo + Icons rechts oben
  size_t freeHeap;
  uint32_t freqMHz;
  int rssi;
  getSystemInfo(freeHeap, freqMHz, rssi);

  // WiFi-Balken
  int bars = wifiBarsFromRSSI(rssi);
  drawWifiIcon(PANEL_WIDTH - 60, 5, bars);

  // Akku rechts oben
  drawBatteryIcon(PANEL_WIDTH - 30, 5, batteryPercent);
}

// -------------------------------
// SD-Liste + Systeminfo unten
// -------------------------------
void drawSDList() {
  // unteren Bereich säubern
  gfx->fillRect(0, 80, PANEL_WIDTH, PANEL_HEIGHT-80, BLACK);

  int y = 90;

  gfx->setTextColor(CYAN, BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(MARGIN_X, y);
  gfx->print("SD card root (scroll mit BOOT):");
  y += 15;

  // Systemmonitor-Zeilen
  size_t freeHeap;
  uint32_t freqMHz;
  int rssi;
  getSystemInfo(freeHeap, freqMHz, rssi);

  gfx->setCursor(MARGIN_X, y);
  gfx->setTextColor(YELLOW, BLACK);
  gfx->print("Heap:");
  gfx->print(freeHeap / 1024);
  gfx->print("kB  CPU:");
  gfx->print(freqMHz);
  gfx->print("MHz");
  y += 12;

  gfx->setCursor(MARGIN_X, y);
  gfx->print("RSSI:");
  gfx->print(rssi);
  gfx->print(" dBm");
  y += 15;

  // SD-Einträge mit Scroll
  gfx->setTextColor(GREEN, BLACK);
  for (int i = 0; i < 10; i++) {
    int idx = scrollIndex + i;
    if (idx >= fileCount) break;
    if (y > PANEL_HEIGHT - 12) break;
    gfx->setCursor(MARGIN_X, y);
    gfx->print(fileLines[idx]);
    y += 12;
  }
}

// -------------------------------
// Scroll-Button (BOOT) auswerten
// -------------------------------
void handleScrollButton() {
  int state = digitalRead(SCROLL_BUTTON);
  if (state == LOW && lastButtonState == HIGH) {
    // kurzer Klick -> nächste Seite
    int step = 5;
    scrollIndex += step;
    if (scrollIndex >= fileCount) scrollIndex = 0;
    drawSDList();
  }
  lastButtonState = state;
}

// -------------------------------
// WLAN verbinden
// -------------------------------
void connectWiFi() {
  WiFi.disconnect(true);
  delay(200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  wifiConnected = false;

  for (int i = 0; i < 60; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      break;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (wifiConnected) {
    Serial.print("WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect FAILED");
  }
}

// -------------------------------
// NTP konfigurieren
// -------------------------------
void setupTime() {
  if (!wifiConnected) {
    timeValid = false;
    return;
  }

  configTime(3600 * timezone, daysavetime * 3600,
             "0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org");

  struct tm t;
  timeValid = false;

  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&t, 1000)) {
      timeValid = true;
      break;
    }
    delay(500);
  }

  if (timeValid)
    Serial.println("NTP synced");
  else
    Serial.println("NTP FAILED");
}

// -------------------------------
// Webserver: SD-Listing & Download
// -------------------------------
void handleRoot() {
  if (!SD_MMC.begin("/sdcard", true)) {
    server.send(500, "text/plain", "SD mount failed");
    return;
  }

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>ESP32 SD Browser</title></head><body>";
  html += "<h2>ESP32-S3 SD-Karte</h2><ul>";

  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    html += "<li><a href='/file?name=" + name + "'>" + name + "</a>";
    if (!file.isDirectory()) {
      html += " (" + String(file.size()) + " B)";
    }
    html += "</li>";
    file = root.openNextFile();
  }
  html += "</ul></body></html>";

  server.send(200, "text/html", html);
}

void handleFileDownload() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing name");
    return;
  }

  String path = server.arg("name");
  if (!SD_MMC.begin("/sdcard", true)) {
    server.send(500, "text/plain", "SD mount failed");
    return;
  }

  File file = SD_MMC.open(path, "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  String contentType = getContentType(path);
  server.streamFile(file, contentType);
  file.close();
}

// -------------------------------
// Setup
// -------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(SCROLL_BUTTON, INPUT_PULLUP);
  lastButtonState = digitalRead(SCROLL_BUTTON);

  gfx->begin();
  gfx->fillScreen(BLACK);

  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(MARGIN_X, 10);
  gfx->print("Booting...");

  // WLAN + Zeit
  connectWiFi();
  setupTime();

  // SD Karte
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD FAIL");
    fileCount = 0;
    fileLines[fileCount++] = "SD mount failed";
  } else {
    buildFileList();
  }

  // Webserver-Routen
  if (wifiConnected) {
    server.on("/", handleRoot);
    server.on("/file", handleFileDownload);
    server.begin();
    Serial.println("HTTP Server started");
    Serial.println("Im Browser: http://<IP-des-Boards>/");
  }

  // erstes Rendern
  gfx->fillScreen(BLACK);
  drawTopBar();
  drawSDList();
}

// -------------------------------
// Loop
// -------------------------------
void loop() {
  static unsigned long lastBar = 0;
  static unsigned long lastWifiCheck = 0;

  unsigned long now = millis();

  // Top-Bar jede Sekunde aktualisieren
  if (now - lastBar > 1000) {
    lastBar = now;
    drawTopBar();
  }

  // WLAN alle 30 Sekunden prüfen
  if (now - lastWifiCheck > 30000) {
    lastWifiCheck = now;

    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      connectWiFi();
      setupTime();
      drawTopBar();
    }
  }

  // Scrollen über BOOT-Button
  handleScrollButton();

  // HTTP-Client bearbeiten (für SD-Webportal)
  if (wifiConnected) {
    server.handleClient();
  }
}
