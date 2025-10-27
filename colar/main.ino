#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>  
#include <HTTPClient.h>
#include <Wire.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "time.h"
#include "esp_wifi.h"

// ===== Modo produção: logs desativáveis =====
#define DEBUG 0
#if DEBUG
  #define LOG(x)        Serial.println(x)
  #define LOGF(fmt, ...) Serial.printf(fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(x)
  #define LOGF(fmt, ...)
#endif

// Habilita modos de sleep (0 = habilita; 1 = desabilita)
#undef DISABLE_SLEEP
#define DISABLE_SLEEP 0

// Estrutura da leitura
struct Reading {
  double latitude;
  double longitude;
  float accel_x;
  float accel_y;
  float accel_z;
  char collected_at[25];
};

// Estado de relógio válido
static bool TIME_READY = false;

// ==========================
// ===== FUNÇÕES TEMPO ======
// ==========================
static bool timeIsValid() {
  time_t now = time(nullptr);
  return now >= 1609459200; // >= 2021-01-01
}

static bool syncTimeOnce(uint32_t timeoutMs = 15000) {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t start = millis();
  while (!timeIsValid() && (millis() - start) < timeoutMs) delay(200);
  return timeIsValid();
}

static String isoNowUTC() {
  time_t now = time(nullptr);
  struct tm tm;
  gmtime_r(&now, &tm);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(buf);
}

// Marca válido se time() já está ok e atualiza referência base
static void markTimeReadyIfValid() {
  if (timeIsValid() && !TIME_READY) TIME_READY = true;
}

// Tenta garantir horário válido (NTP) com timeout
static bool ensureTimeReady(uint32_t timeoutMs = 15000) {
  if (timeIsValid()) { markTimeReadyIfValid(); return true; }
  if (!WiFi.isConnected()) return false;
  if (!syncTimeOnce(timeoutMs)) return false;
  markTimeReadyIfValid();
  return TIME_READY;
}

// ==========================
// ===== CONFIGURAÇÕES ======
// ==========================

// Wi-Fi
WiFiMulti wifiMulti;
struct WifiCredential { const char* ssid; const char* pass; };
WifiCredential WIFI_LIST[] = {
  {"RICARDO", "Paocombanha23*"},
};

// API
static const char* API_URL = "https://api.seu-dominio.com/readings";
static const char* API_KEY = "REDACTED_TOKEN";
static const char* API_KEY_HEADER = "Authorization";

static const char ROOT_CA[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIF...COLE_AQUI_A_CA_COMPLETA...Q==
-----END CERTIFICATE-----
)PEM";

// SD
#define SD_CS_PIN 21
static const char* DIR_QUEUE = "/queue";
static const char* DIR_DEAD = "/deadletter";

// MMA7660FC
#define MMA7660_ADDR 0x4C
#define MMA7660_MODE 0x07
#define MMA7660_XOUT 0x00
#define MMA7660_YOUT 0x01
#define MMA7660_ZOUT 0x02

// Intervalos (mantidos)
static const uint32_t SAMPLE_INTERVAL_MS  = 10000;        // 10s
static const size_t   BATCH_SIZE          = 40;           // 40 leituras
static const uint32_t UPLOAD_INTERVAL_MS  = 20*60*1000;   // 20 min
static const long     GMT_OFFSET_SEC      = -3*3600;      // UTC-3

#define uS_TO_S_FACTOR 1000000ULL

// ==========================
// ===== VARIÁVEIS =========
// ==========================
Reading  batch[BATCH_SIZE];
size_t   batchCount = 0;

uint32_t lastSampleMs   = 0;
uint32_t lastUploadMs   = 0;

// ==========================
// ===== FUNÇÕES MMA7660 ====
// ==========================
bool initAccel() {
  Wire.begin();
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(MMA7660_MODE);
  Wire.write(0x01); // modo ativo
  return Wire.endTransmission() == 0;
}

float readAxis(uint8_t axisReg) {
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(axisReg);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(MMA7660_ADDR, (uint8_t)1, true);
  if (Wire.available()) {
    int8_t val = Wire.read();
    if (val > 31) val -= 64; // ajuste de sinal
    return ((float)val * 1.5) / 32.0; // em "g"
  }
  return 0;
}

void readAccel(float &x, float &y, float &z) {
  x = readAxis(MMA7660_XOUT);
  y = readAxis(MMA7660_YOUT);
  z = readAxis(MMA7660_ZOUT);
}

// ==========================
// ===== FUNÇÕES SD =========
// ==========================
void ensureDirs() {
  if (!SD.exists(DIR_QUEUE)) SD.mkdir(DIR_QUEUE);
  if (!SD.exists(DIR_DEAD))  SD.mkdir(DIR_DEAD);
}

String makeBatchFilename() {
  time_t now = time(nullptr);
  struct tm tmnow;
  gmtime_r(&now, &tmnow);
  char buf[48];
  sprintf(buf, "%s/batch_%04d%02d%02d_%02d%02d%02d.json",
          DIR_QUEUE,
          tmnow.tm_year + 1900, tmnow.tm_mon + 1, tmnow.tm_mday,
          tmnow.tm_hour, tmnow.tm_min, tmnow.tm_sec);
  return String(buf);
}

void writeReadingJSON(Stream &s, const Reading &r) {
  s.print("{");
  s.print("\"accel_x\":"); s.print(r.accel_x, 3);
  s.print(",\"accel_y\":"); s.print(r.accel_y, 3);
  s.print(",\"accel_z\":"); s.print(r.accel_z, 3);
  s.print(",\"collected_at\":\""); s.print(r.collected_at); s.print("\"");
  s.print("}");
}

bool flushBatchToSD() {
  if (batchCount == 0) return true;
  String path = makeBatchFilename();
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.print("{\"readings\":[");
  for (size_t i=0; i<batchCount; i++) {
    if (i>0) f.print(",");
    writeReadingJSON(f,batch[i]);
  }
  f.print("]}");
  f.close();
  batchCount = 0;
  return true;
}

// ==========================
// ===== FUNÇÕES WIFI =======
// ==========================
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  for (auto &w: WIFI_LIST) wifiMulti.addAP(w.ssid,w.pass);
  uint8_t st = wifiMulti.run();
  if (st == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
    return true;
  }
  return false;
}

// ==========================
// ===== FUNÇÕES API ========
// ==========================
String nextQueueFile() {
  File dir = SD.open(DIR_QUEUE); 
  if (!dir || !dir.isDirectory()) return String();
  String first;
  while (true) {
    File e = dir.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      String name = String(DIR_QUEUE) + "/" + String(e.name());
      if (first.isEmpty() || name < first) first = name;
    }
    e.close();
  }
  dir.close();
  return first;
}

bool postFileToAPI(const String &path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  HTTPClient http;
  if (!http.begin(client, API_URL)) { f.close(); return false; }
  http.addHeader("Content-Type", "application/json");
  http.addHeader(API_KEY_HEADER, String("Bearer ") + API_KEY);
  int code = http.sendRequest("POST", &f, f.size());
  f.close();
  if (code >= 200 && code < 300) { SD.remove(path); http.end(); return true; }
  http.end();
  return false;
}

void tryUploadQueue() {
  if (!ensureWiFi()) return;
  if (!TIME_READY) syncTimeOnce(10000);
  while (true) {
    String path = nextQueueFile();
    if (path.isEmpty()) break;
    if (!postFileToAPI(path)) break;
    delay(100);
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
}

// ==========================
// ===== SETUP ==============
// ==========================
void setup() {
#if DEBUG
  Serial.begin(115200);
  delay(300);
#endif

  SD.begin(SD_CS_PIN);
  ensureDirs();
  initAccel();

  lastSampleMs = millis();
  lastUploadMs = millis();

  // começa a registrar antes do Wi-Fi
  ensureWiFi();
  ensureTimeReady(15000);
}

// ==========================
// ===== LOOP ===============
// ==========================
void loop() {
  uint32_t now = millis();

  // coleta a cada 10s
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    Reading r;
    r.latitude = 0.0;
    r.longitude = 0.0;
    strncpy(r.collected_at, isoNowUTC().c_str(), 24);
    readAccel(r.accel_x, r.accel_y, r.accel_z);
    batch[batchCount++] = r;
    if (batchCount >= BATCH_SIZE) flushBatchToSD();
  }

  // controle de energia / sincronização
  struct tm timeinfo;
  bool podeSincronizar = true;
  if (getLocalTime(&timeinfo)) {
    int hourNow = timeinfo.tm_hour;
        // para de tentar enviar os dados via Wi-Fi  (08h–15h)
    if (hourNow >= 8 && hourNow < 15) podeSincronizar = false;

    // Deep sleep longo  (22h–03h)
    if (hourNow >= 23 || hourNow < 4) {
      if (batchCount > 0) flushBatchToSD();
      tryUploadQueue();
#if !DISABLE_SLEEP
      esp_sleep_enable_timer_wakeup(6 * 60 * 60 * uS_TO_S_FACTOR); // 6h
      esp_deep_sleep_start();
#endif
    }
  }

  // Upload periódico
  if (podeSincronizar && (now - lastUploadMs >= UPLOAD_INTERVAL_MS)) {
    lastUploadMs = now;
    if (batchCount > 0 && batchCount >= BATCH_SIZE/2) flushBatchToSD();
    tryUploadQueue();
    markTimeReadyIfValid();
  }

  // Light sleep curto entre eventos
  uint32_t nextSample = (lastSampleMs + SAMPLE_INTERVAL_MS > now)
                        ? (lastSampleMs + SAMPLE_INTERVAL_MS - now) : 0;
  uint32_t nextUpload = (lastUploadMs + UPLOAD_INTERVAL_MS > now)
                        ? (lastUploadMs + UPLOAD_INTERVAL_MS - now) : 0;
  uint32_t sleepMs = (nextSample == 0) ? nextUpload : ((nextUpload == 0) ? nextSample : min(nextSample, nextUpload));
  if (sleepMs == 0) sleepMs = 20;

#if !DISABLE_SLEEP
  esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
  esp_light_sleep_start();
#else
  delay(sleepMs);
#endif
}
