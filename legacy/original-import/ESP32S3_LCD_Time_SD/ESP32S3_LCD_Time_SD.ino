#include <WiFi.h>
#include <time.h>

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#define ENABLE_FTP 1
#if ENABLE_FTP
  #include <SimpleFTPServer.h>
  FtpServer ftpSrv;
#endif

// ------------------ Farben ------------------
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFD20

// ------------------ WLAN / Zeit ------------------
// STA: verbindet sich mit deinem WLAN
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Gerät / Hostname / mDNS
const char* deviceName   = "YOUR_AP_SSID";     // WiFi Hostname (Router-Clientliste)
const char* mdnsName     = "nocloudhub";     // mDNS (http://nocloudhub.local)

// Fallback AP (wenn WLAN nicht klappt)
const char* ap_ssid      = "YOUR_AP_SSID";     // eigenes WLAN
const char* ap_password  = "YOUR_PASSWORD"; // >=8 Zeichen, hier bewusst gleich wie STA-Passwort

// Zeit
long timezoneOffset = 1;   // UTC+1
byte daylight       = 1;   // Sommerzeit

bool wifiConnected  = false;
bool apStarted      = false;
bool mdnsStarted    = false;

// Fallback-Logik
const uint32_t WIFI_CONNECT_WINDOW_MS = 2UL * 60UL * 1000UL; // 2 Minuten bis AP startet
unsigned long wifiFirstAttemptMs      = 0;
unsigned long lastReconnectAttemptMs  = 0;

// ------------------ Display / Pins ------------------
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS   42
#define TFT_DC   41
#define TFT_RST  39
#define TFT_BL   48

// SD-Pins (korrekt)
#define SD_CS    21
#define SD_SCK   14
#define SD_MOSI  15
#define SD_MISO  16

// eigener SPI-Bus für SD
SPIClass sdSPI(HSPI);

// Panelgröße – 1.47" ST7789, effektiv 172x320
#define PANEL_WIDTH  172
#define PANEL_HEIGHT 320

#define BTN_BOOT 0   // BOOT-Taste

Arduino_ESP32SPI *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED
);

// ST7789: Controller hat 240x320, sichtbarer Bereich 172 breit.
Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  TFT_RST,
  3,         // Rotation (quer, USB nach oben)
  true,      // IPS
  PANEL_WIDTH,
  PANEL_HEIGHT,
  34, 0,     // col_offset1, row_offset1
  34, 0      // col_offset2, row_offset2
);

// etwas weiter vom Rand weg, wegen Abrundung → 16px
const int MARGIN_X = 16;

// ------------------ Seiten / UI ------------------
int currentPage = 0;           // 0: Uhr, 1: Netzwerk, 2: SD
int lastButtonState = HIGH;

// ------------------ Systeminfos ------------------
int batteryPercent = 100;      // Platzhalter
bool onExternalPower = true;   // USB-Stick → immer true

// ------------------ SD-Infos ------------------
bool sdMounted = false;
uint64_t sdCardSizeBytes = 0;
uint64_t sdUsedBytes     = 0;
uint64_t sdFreeBytes     = 0;

String fileLines[128];
int fileCount   = 0;
int scrollIndex = 0;

// ------------------ Netzwerk / Webserver ------------------
WebServer server(80);

// MIME-Type
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

// ------------------ Helfer: Systeminfos ------------------
void getSystemInfo(size_t &freeHeap, uint32_t &freqMHz, int &rssi) {
  freeHeap = ESP.getFreeHeap();
  freqMHz  = getCpuFrequencyMhz();
  rssi     = (wifiConnected ? WiFi.RSSI() : -100);
}

int wifiBarsFromRSSI(int rssi) {
  if (!wifiConnected) return 0;
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

void drawWifiIcon(int x, int y, int bars) {
  int w = 4;
  int h = 3;
  for (int i = 0; i < 4; i++) {
    uint16_t color = (i < bars) ? GREEN : BLUE;
    gfx->fillRect(x + i*(w+1), y + (3-i)*h, w, h, color);
  }
}

void drawBatteryIcon(int x, int y, int percent) {
  gfx->drawRect(x, y, 22, 9, WHITE);
  gfx->fillRect(x+22, y+2, 2, 5, WHITE);
  int innerWidth = 22 - 2;
  int filled = (innerWidth * percent) / 100;
  gfx->fillRect(x+1, y+1, filled, 7, (percent > 20 ? GREEN : RED));
}

void drawPowerIcon(int x, int y) {
  gfx->drawLine(x,   y,   x+3, y+6, YELLOW);
  gfx->drawLine(x+3, y+6, x+1, y+6, YELLOW);
  gfx->drawLine(x+1, y+6, x+4, y+12, YELLOW);
}

// Icons oben rechts
void drawStatusIcons() {
  size_t freeHeap;
  uint32_t freqMHz;
  int rssi;
  getSystemInfo(freeHeap, freqMHz, rssi);
  int bars = wifiBarsFromRSSI(rssi);

  int baseX = PANEL_WIDTH - 70;
  drawWifiIcon(baseX, 6, bars);
  drawBatteryIcon(baseX+25, 6, batteryPercent);
  if (onExternalPower) {
    drawPowerIcon(baseX+10, 18);
  }
}

// ------------------ SD Größe berechnen ------------------
void accumulateSizeRecursive(const String &path) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;

  File f = dir.openNextFile();
  while (f) {
    if (f.isDirectory()) {
      String sub = String(f.path());
      accumulateSizeRecursive(sub);
    } else {
      sdUsedBytes += f.size();
    }
    f = dir.openNextFile();
  }
}

void computeSDStats() {
  if (!sdMounted) return;
  sdCardSizeBytes = SD.cardSize();
  sdUsedBytes = 0;
  accumulateSizeRecursive("/");
  if (sdCardSizeBytes > sdUsedBytes)
    sdFreeBytes = sdCardSizeBytes - sdUsedBytes;
  else
    sdFreeBytes = 0;
}

// ------------------ SD-Dateiliste ------------------
void buildFileList() {
  fileCount = 0;
  if (!sdMounted) {
    fileLines[fileCount++] = "SD not mounted";
    return;
  }

  File root = SD.open("/");
  if (!root) {
    fileLines[fileCount++] = "SD open failed";
    return;
  }

  File file = root.openNextFile();
  while (file && fileCount < 128) {
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
    if (line.length() > 28)
      line = line.substring(0, 27) + ">";
    fileLines[fileCount++] = line;
    file = root.openNextFile();
  }

  if (fileCount == 0)
    fileLines[fileCount++] = "SD empty";

  scrollIndex = 0;
}

// ------------------ mDNS ------------------
void startMDNSIfNeeded() {
  if (mdnsStarted) return;

  // mDNS macht nur Sinn, wenn irgendein Interface aktiv ist (STA oder AP)
  if (!wifiConnected && !apStarted) return;

  if (MDNS.begin(mdnsName)) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    Serial.print("mDNS OK: http://");
    Serial.print(mdnsName);
    Serial.println(".local/");
  } else {
    Serial.println("mDNS FAILED (retry later)");
  }
}

// ------------------ WLAN / AP ------------------
void connectWiFiOnce() {
  // STA-Modus, kein AP sofort
  WiFi.mode(WIFI_STA);

  // Hostname (Router-Clientliste / DHCP)
  WiFi.setHostname(deviceName);

  WiFi.begin(ssid, password);
  Serial.print("Connecting STA to WiFi");
}

void startFallbackAP() {
  if (apStarted) return;

  // AP + STA gleichzeitig möglich; AP starten als Fallback
  WiFi.mode(WIFI_AP_STA);

  // AP starten
  bool ok = WiFi.softAP(ap_ssid, ap_password);
  apStarted = ok;

  if (ok) {
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP started: ");
    Serial.print(ap_ssid);
    Serial.print("  IP: ");
    Serial.println(apIP);
  } else {
    Serial.println("AP start FAILED");
  }

  // mDNS versuchen (auch im AP-Modus nützlich)
  startMDNSIfNeeded();
}

void stopFallbackAP() {
  if (!apStarted) return;

  // AP aus (sicherer), wenn STA wieder da ist
  WiFi.softAPdisconnect(true);
  apStarted = false;
  Serial.println("AP stopped (STA connected)");
}

// ------------------ Zeit ------------------
void setupTime() {
  if (!wifiConnected) return;

  configTime(3600 * timezoneOffset,
             daylight * 3600,
             "0.pool.ntp.org",
             "1.pool.ntp.org",
             "2.pool.ntp.org");

  struct tm t;
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&t, 1000)) {
      Serial.println("NTP synced");
      return;
    }
    delay(500);
  }
  Serial.println("NTP FAILED");
}

// ------------------ HTTP / FTP ------------------
void handleRoot() {
  if (!sdMounted) {
    server.send(500, "text/plain", "SD not mounted");
    return;
  }

  File root = SD.open("/");
  if (!root) {
    server.send(500, "text/plain", "SD open failed");
    return;
  }

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>NoCloudHub SD Browser</title></head><body>";
  html += "<h2>NoCloudHub – ESP32-S3 SD-Karte</h2>";
  html += "<p>mDNS: <b>http://";
  html += String(mdnsName);
  html += ".local/</b></p><ul>";

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
  File file = SD.open(path, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  String contentType = getContentType(path);
  server.streamFile(file, contentType);
  file.close();
}

void startHttpFtpIfNeeded() {
  static bool started = false;
  if (started) return;

  // HTTP immer starten (auch AP-only)
  server.on("/", handleRoot);
  server.on("/file", handleFileDownload);
  server.begin();

#if ENABLE_FTP
  // FTP User ändern: yanis / Passwort bleibt wie gewünscht
  ftpSrv.begin("USER", "CHANGEME");
#endif

  started = true;
  Serial.println("HTTP/FTP started");
}

// ------------------ UI: Zeitblock ------------------
void drawTimeBlock() {
  gfx->fillScreen(BLACK);

  struct tm t;
  bool got = getLocalTime(&t, 150);

  if (!got) {
    gfx->setTextSize(3);
    gfx->setTextColor(RED, BLACK);
    gfx->setCursor(MARGIN_X, 60);
    gfx->print("NO TIME");
    drawStatusIcons();
    return;
  }

  int hour   = t.tm_hour;
  int minute = t.tm_min;
  int second = t.tm_sec;

  uint16_t hourColor = (hour >= 7 && hour <= 16) ? GREEN : BLUE;

  uint16_t minuteColor;
  if (minute <= 15)      minuteColor = GREEN;
  else if (minute <=30 ) minuteColor = YELLOW;
  else if (minute <=45 ) minuteColor = ORANGE;
  else                   minuteColor = MAGENTA;

  char buf[8];

  int timeSize = 4;
  gfx->setTextSize(timeSize);

  int yTime = 70;

  gfx->setCursor(MARGIN_X, yTime);
  gfx->setTextColor(hourColor, BLACK);
  snprintf(buf, sizeof(buf), "%02d", hour);
  gfx->print(buf);

  gfx->setTextColor(WHITE, BLACK);
  gfx->print(":");

  gfx->setTextColor(minuteColor, BLACK);
  snprintf(buf, sizeof(buf), "%02d", minute);
  gfx->print(buf);

  gfx->setTextColor(WHITE, BLACK);
  gfx->print(":");

  snprintf(buf, sizeof(buf), "%02d", second);
  gfx->print(buf);

  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  int yDate = yTime + 8 * timeSize + 24;
  gfx->setCursor(MARGIN_X, yDate);
  char dateBuf[20];
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  gfx->print(dateBuf);

  drawStatusIcons();
}

// ------------------ Seite 1: Uhr ------------------
void drawPage1() {
  drawTimeBlock();
}

// ------------------ Seite 2: Netzwerk / System ------------------
void drawPage2() {
  gfx->fillScreen(BLACK);
  drawStatusIcons();

  int x = MARGIN_X;
  int y = 40;

  gfx->setTextSize(2);
  gfx->setTextColor(CYAN, BLACK);
  gfx->setCursor(x, y);
  gfx->print("Netzwerk");
  y += 22;

  gfx->setTextSize(1);

  // STA Status
  gfx->setCursor(x, y);
  gfx->setTextColor(CYAN, BLACK);
  gfx->print("STA SSID: ");
  gfx->print(ssid);
  y += 10;

  gfx->setCursor(x, y);
  gfx->setTextColor(WHITE, BLACK);
  gfx->print("STA IP : ");
  if (wifiConnected) gfx->print(WiFi.localIP());
  else               gfx->print("disconnected");
  y += 12;

  // AP Status
  gfx->setCursor(x, y);
  gfx->setTextColor(MAGENTA, BLACK);
  gfx->print("AP SSID: ");
  gfx->print(ap_ssid);
  y += 10;

  gfx->setCursor(x, y);
  gfx->setTextColor(WHITE, BLACK);
  gfx->print("AP IP  : ");
  if (apStarted) gfx->print(WiFi.softAPIP());
  else           gfx->print("off");
  y += 12;

  // URLs
  gfx->setCursor(x, y);
  gfx->setTextColor(GREEN, BLACK);
  gfx->print("mDNS: http://");
  gfx->print(mdnsName);
  gfx->print(".local/");
  y += 12;

  if (wifiConnected) {
    IPAddress ip = WiFi.localIP();
    char ipBuf[32];
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    gfx->setCursor(x, y);
    gfx->setTextColor(GREEN, BLACK);
    gfx->print("HTTP: http://");
    gfx->print(ipBuf);
    gfx->print(":80/");
    y += 10;

    gfx->setCursor(x, y);
    gfx->setTextColor(MAGENTA, BLACK);
    gfx->print("FTP : ftp://yanis@");
    gfx->print(ipBuf);
    gfx->print(":21/");
    y += 12;
  } else if (apStarted) {
    IPAddress ip = WiFi.softAPIP();
    char ipBuf[32];
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    gfx->setCursor(x, y);
    gfx->setTextColor(GREEN, BLACK);
    gfx->print("HTTP: http://");
    gfx->print(ipBuf);
    gfx->print(":80/");
    y += 10;

    gfx->setCursor(x, y);
    gfx->setTextColor(MAGENTA, BLACK);
    gfx->print("FTP : ftp://yanis@");
    gfx->print(ipBuf);
    gfx->print(":21/");
    y += 12;
  }

  size_t freeHeap;
  uint32_t freqMHz;
  int rssi;
  getSystemInfo(freeHeap, freqMHz, rssi);

  gfx->setTextColor(YELLOW, BLACK);
  gfx->setCursor(x, y);
  gfx->print("Heap: ");
  gfx->print(freeHeap/1024);
  gfx->print(" kB");
  y += 10;

  gfx->setCursor(x, y);
  gfx->print("CPU : ");
  gfx->print(freqMHz);
  gfx->print(" MHz");
  y += 10;

  gfx->setCursor(x, y);
  gfx->print("RSSI: ");
  gfx->print(rssi);
  gfx->print(" dBm");
}

// ------------------ Seite 3: SD Analyse + Liste ------------------
void drawPage3() {
  gfx->fillScreen(BLACK);
  drawStatusIcons();

  int x = MARGIN_X;
  int y = 40;

  gfx->setTextSize(2);
  gfx->setTextColor(CYAN, BLACK);
  gfx->setCursor(x, y);
  gfx->print("SD-Analyse");
  y += 22;

  gfx->setTextSize(1);

  if (!sdMounted) {
    gfx->setCursor(x, y);
    gfx->setTextColor(RED, BLACK);
    gfx->print("SD not mounted");
    return;
  }

  uint64_t totalMB = sdCardSizeBytes / (1024ULL*1024ULL);
  uint64_t usedMB  = sdUsedBytes     / (1024ULL*1024ULL);
  uint64_t freeMB  = sdFreeBytes     / (1024ULL*1024ULL);

  gfx->setCursor(x, y);
  gfx->setTextColor(WHITE, BLACK);
  gfx->print("Gesamt: ");
  gfx->print(totalMB);
  gfx->print(" MB");
  y += 10;

  gfx->setCursor(x, y);
  gfx->print("Belegt: ");
  gfx->print(usedMB);
  gfx->print(" MB");
  y += 10;

  gfx->setCursor(x, y);
  gfx->print("Frei  : ");
  gfx->print(freeMB);
  gfx->print(" MB");
  y += 10;

  gfx->setCursor(x, y);
  gfx->setTextColor(YELLOW, BLACK);
  gfx->print("FAT: n/a   Cluster: n/a");
  y += 12;

  gfx->setCursor(x, y);
  gfx->setTextColor(CYAN, BLACK);
  gfx->print("SD root/");
  y += 10;

  gfx->setTextColor(GREEN, BLACK);
  for (int i = 0; i < 10; i++) {
    int idx = scrollIndex + i;
    if (idx >= fileCount) break;
    if (y > PANEL_HEIGHT - 12) break;
    gfx->setCursor(x, y);
    gfx->print(fileLines[idx]);
    y += 10;
  }
}

// ------------------ Boot-Ladebalken ------------------
void drawBootProgress(int percent, const char* label) {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(MARGIN_X, 50);
  gfx->print("Booting...");

  gfx->setTextSize(1);
  gfx->setCursor(MARGIN_X, 70);
  gfx->print(label);

  int barX = MARGIN_X;
  int barY = 90;
  int barW = PANEL_WIDTH - 2*MARGIN_X;
  int barH = 10;

  gfx->drawRect(barX, barY, barW, barH, WHITE);
  int fillW = (barW-2) * percent / 100;
  gfx->fillRect(barX+1, barY+1, fillW, barH-2, GREEN);
}

// ------------------ Button-Handling ------------------
void handleButton() {
  int state = digitalRead(BTN_BOOT);
  if (state == LOW && lastButtonState == HIGH) {
    if (currentPage == 0) {
      currentPage = 1;
    } else if (currentPage == 1) {
      currentPage = 2;
      scrollIndex = 0;
    } else if (currentPage == 2) {
      int linesPerPage = 10;
      int step = 5;
      if (scrollIndex + linesPerPage < fileCount) {
        scrollIndex += step;
        if (scrollIndex >= fileCount) scrollIndex = fileCount - 1;
      } else {
        currentPage = 0;
        scrollIndex = 0;
      }
    }
  }
  lastButtonState = state;
}

// ------------------ setup ------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(BTN_BOOT, INPUT_PULLUP);
  lastButtonState = digitalRead(BTN_BOOT);

  gfx->begin();
  gfx->fillScreen(BLACK);

  drawBootProgress(5, "Init Display");

  drawBootProgress(20, "WiFi STA start");
  wifiFirstAttemptMs = millis();
  connectWiFiOnce();

  // Kurzer initialer Connect-Versuch (ohne 2 Minuten zu blockieren)
  drawBootProgress(30, "STA connect check");
  for (int i = 0; i < 40; i++) { // ~10 Sekunden
    if (WiFi.status() == WL_CONNECTED) break;
    delay(250);
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    Serial.print("STA OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("STA not connected (will keep trying)");
  }

  drawBootProgress(40, "NTP (wenn STA)");
  setupTime();

  drawBootProgress(55, "SD initialisieren");
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSPI)) {
    sdMounted = true;
    computeSDStats();
    buildFileList();
  } else {
    sdMounted = false;
    Serial.println("SD mount failed");
  }

  drawBootProgress(80, "HTTP/FTP starten");
  startHttpFtpIfNeeded();

  // mDNS versuchen (falls STA schon da)
  startMDNSIfNeeded();

  drawBootProgress(100, "Fertig");
  delay(600);

  currentPage = 0;
}

// ------------------ loop ------------------
void loop() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();

  // UI refresh
  if (now - lastUpdate > 1000) {
    lastUpdate = now;
    if (currentPage == 0)      drawPage1();
    else if (currentPage == 1) drawPage2();
    else if (currentPage == 2) drawPage3();
  }

  // STA Status aktualisieren
  bool staNow = (WiFi.status() == WL_CONNECTED);
  if (staNow && !wifiConnected) {
    wifiConnected = true;
    Serial.print("STA re-connected, IP: ");
    Serial.println(WiFi.localIP());

    // AP aus (sicherer), sobald STA wieder ok
    stopFallbackAP();

    // Zeit + mDNS
    setupTime();
    startMDNSIfNeeded();
  } else if (!staNow && wifiConnected) {
    wifiConnected = false;
    Serial.println("STA disconnected");
  }

  // Reconnect-Versuche alle 30 Sekunden
  if (!wifiConnected && (now - lastReconnectAttemptMs > 30000)) {
    lastReconnectAttemptMs = now;
    Serial.println("STA reconnect attempt...");
    connectWiFiOnce();
  }

  // Fallback AP nach 2 Minuten ohne STA
  if (!wifiConnected && !apStarted && (now - wifiFirstAttemptMs > WIFI_CONNECT_WINDOW_MS)) {
    Serial.println("STA failed long enough -> starting AP fallback");
    startFallbackAP();
  }

  // mDNS ggf. nochmal versuchen (falls vorher fehlgeschlagen)
  if (!mdnsStarted && (wifiConnected || apStarted)) {
    startMDNSIfNeeded();
  }

  // Button
  handleButton();

  // HTTP/FTP
  server.handleClient();
#if ENABLE_FTP
  ftpSrv.handleFTP();
#endif
}
