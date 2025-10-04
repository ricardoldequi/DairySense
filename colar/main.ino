#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "time.h"

// ==========================
// ===== CONFIGURAÇÕES ======
// ==========================

// Wi-Fi
WiFiMulti wifiMulti;
struct WifiCredential { const char* ssid; const char* pass; };
WifiCredential WIFI_LIST[] = {
  {"RICARDO", "Paocombanha23*"},
  // {"OutraRede", "SenhaAqui"}
};

// API
static const char* API_URL = "https://5f3b2fd75bc7.ngrok-free.app/readings";
static const char* API_KEY = "58567df6-233b-4607-971f-d80b6ca927a2";
static const char* API_KEY_HEADER = "Authorization";

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

// Intervalos
static const uint32_t SAMPLE_INTERVAL_MS = 10000;   // 10 segundos
static const size_t BATCH_SIZE = 40;               // 40 leituras por batch
static const uint32_t UPLOAD_INTERVAL_MS = 20*60*1000; // 20 minutos
static const long GMT_OFFSET_SEC = -3*3600; // UTC-3

// Deep Sleep
#define uS_TO_S_FACTOR 1000000ULL

// ==========================
// ===== VARIÁVEIS =========
// ==========================
struct Reading {
  double latitude;
  double longitude;
  float accel_x;
  float accel_y;
  float accel_z;
  char collected_at[25];
};

Reading batch[BATCH_SIZE];
size_t batchCount = 0;

uint32_t lastSampleMs = 0;
uint32_t lastUploadMs = 0;
time_t lastSyncedTime = 0;   // timestamp NTP
uint32_t lastSyncMillis = 0;  // millis() correspondente

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

//Lê um eixo do acelerômetro (X, Y ou Z)
float readAxis(uint8_t axisReg) {
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(axisReg);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(MMA7660_ADDR, (uint8_t)1, true);
  if (Wire.available()) {
    int8_t val = Wire.read();
    if (val > 31) val -= 64; // ajuste de sinal
    return ((float)val * 1.5) / 32.0; // "g"
  }
  return 0;
}

// Lê todos os eixos do acelerômetro e armazena em x, y, z
void readAccel(float &x, float &y, float &z) {
  x = readAxis(MMA7660_XOUT);
  y = readAxis(MMA7660_YOUT);
  z = readAxis(MMA7660_ZOUT);
}

// ==========================
// ===== FUNÇÕES TEMPO ======
// ==========================
// Retorna o horário atual em formato ISO 8601 (UTC)
String getCurrentTimeISO() {
  time_t now;
  if (lastSyncedTime > 0) {
    now = lastSyncedTime + ((millis() - lastSyncMillis)/1000);
  } else {
    now = time(nullptr);
  }
  struct tm tmnow;
  gmtime_r(&now, &tmnow);
  char buf[25];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          tmnow.tm_year + 1900, tmnow.tm_mon + 1, tmnow.tm_mday,
          tmnow.tm_hour, tmnow.tm_min, tmnow.tm_sec);
  return String(buf);
}

// ==========================
// ===== FUNÇÕES SD =========
// ==========================
// Garante que os diretórios /queue e /deadletter existem no SD
void ensureDirs() {
  if (!SD.exists(DIR_QUEUE)) SD.mkdir(DIR_QUEUE);
  if (!SD.exists(DIR_DEAD)) SD.mkdir(DIR_DEAD);
}

// Gera um nome único para o arquivo de batch, baseado no horário e millis()
String makeBatchFilename() {
  time_t now = lastSyncedTime + ((millis() - lastSyncMillis)/1000);
  struct tm tmnow;
  gmtime_r(&now, &tmnow);
  char buf[48];
  sprintf(buf, "%s/batch_%04d%02d%02d_%02d%02d%02d_%lu.json",
          DIR_QUEUE,
          tmnow.tm_year + 1900, tmnow.tm_mon + 1, tmnow.tm_mday,
          tmnow.tm_hour, tmnow.tm_min, tmnow.tm_sec,
          (unsigned long)millis());
  return String(buf);
}

// Escreve uma leitura em formato JSON no formato necessário
void writeReadingJSON(Stream &s, const Reading &r) {
  s.print("{");
  s.print("\"latitude\":"); s.print(r.latitude, 6);
  s.print(",\"longitude\":"); s.print(r.longitude, 6);
  s.print(",\"accel_x\":"); s.print(r.accel_x, 3);
  s.print(",\"accel_y\":"); s.print(r.accel_y, 3);
  s.print(",\"accel_z\":"); s.print(r.accel_z, 3);
  s.print(",\"collected_at\":\""); s.print(r.collected_at); s.print("\"");
  s.print("}");
}

// Salva o batch atual no SD como arquivo JSON
bool flushBatchToSD() {
  if (batchCount == 0) return true;
  String path = makeBatchFilename();
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.println("❌ Falha ao criar arquivo no SD"); return false; }
  f.print("{\"readings\":[");
  for (size_t i=0; i<batchCount; i++) {
    if (i>0) f.print(",");
    writeReadingJSON(f,batch[i]);
  }
  f.print("]}");
  f.flush();
  f.close();
  Serial.print("✅ Batch salvo: "); Serial.println(path);
  batchCount = 0;
  return true;
}

// ==========================
// ===== FUNÇÕES WIFI =======
// ==========================
// Garante conexão Wi-Fi; retorna true se conectado
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.print("Conectando Wi-Fi...");
  configTime(GMT_OFFSET_SEC,0,"pool.ntp.org");
  uint8_t st = wifiMulti.run();
  if (st == WL_CONNECTED) {
    Serial.println(" conectado!");
    lastSyncedTime = time(nullptr);
    lastSyncMillis = millis();
    return true;
  }
  Serial.println(" falhou.");
  return false;
}

// ==========================
// ===== FUNÇÕES API ========
// ==========================
// Retorna o nome do próximo arquivo na fila de envio (/queue)
String nextQueueFile() {
  File dir = SD.open(DIR_QUEUE); if (!dir || !dir.isDirectory()) return String();
  String first;
  while(true) {
    File e = dir.openNextFile();
    if(!e) break;
    if(!e.isDirectory()) {
      String name = String(DIR_QUEUE) + "/" + String(e.name());
      if(first.length()==0 || name < first) first = name;
    }
    e.close();
  }
  dir.close(); return first;
}

// Move um arquivo para a pasta /deadletter em caso de erro permanente
void moveToDeadletter(const String &path) {
  String fname = path.substring(path.lastIndexOf('/')+1);
  String dst = String(DIR_DEAD)+"/"+fname;
  SD.remove(dst);
  if(SD.rename(path,dst)) Serial.print("⚠️ Movido para deadletter: "), Serial.println(dst);
  else Serial.print("❌ Falha ao mover para deadletter: "), Serial.println(path);
}

// Envia um arquivo batch para a API via HTTP POST
bool postFileToAPI(const String &path) {
  File f = SD.open(path, FILE_READ);
  if(!f) { Serial.print("❌ Não abriu arquivo: "); Serial.println(path); return false; }

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  if(!http.begin(client, API_URL)) { Serial.println("❌ Falha em http.begin()"); f.close(); http.end(); return false; }

  http.addHeader("Content-Type","application/json");
  http.addHeader(API_KEY_HEADER, API_KEY);

  int httpCode = http.sendRequest("POST",&f,f.size());
  Serial.print("➡️ POST "); Serial.print(API_URL); Serial.print(" → "); Serial.println(httpCode);

  f.close();
  String resp = http.getString(); if(resp.length()){Serial.println("📩 Resposta: "+resp);}

  if(httpCode>=200 && httpCode<300) { SD.remove(path); http.end(); return true; }
  if(httpCode>=400 && httpCode<500) { moveToDeadletter(path); http.end(); return false; }
  http.end(); return false;
}

// Tenta enviar todos os arquivos pendentes na fila para a API
void tryUploadQueue() {
  if(!ensureWiFi()) return;
  while(true) {
    String path = nextQueueFile();
    if(path.length()==0) break;
    if(!postFileToAPI(path)) break;
    delay(50);
  }
  // Desconecta Wi-Fi para economizar bateria
  Serial.println("🔌 Desconectando Wi-Fi para economia");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
}

// ==========================
// ===== SETUP ==============
// ==========================
// Inicializa SD, Wi-Fi, acelerômetro e variáveis de tempo
void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n=== COLAR ESP32 - MMA7660 + SD Batch ===");

  if(!SD.begin(SD_CS_PIN)) Serial.println("❌ Falha ao montar SD");
  else { Serial.println("✅ SD montado"); ensureDirs(); }

  for(auto &w: WIFI_LIST) if(w.ssid && strlen(w.ssid)) wifiMulti.addAP(w.ssid,w.pass);

  if(!initAccel()) Serial.println("❌ Erro inicializando acelerômetro"); else Serial.println("✅ Acelerômetro pronto");

  lastSampleMs = millis();
  lastUploadMs = millis();
}

// ==========================
// ===== LOOP ===============
// ==========================
// coleta leituras, salva batches, faz upload condicional e gerencia sleep
vvoid loop() {
  uint32_t now = millis();

  //Leitura e gravação em batch
  if(now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    Reading r;
    r.latitude = 0.0;
    r.longitude = 0.0;
    r.collected_at[0] = 0;
    strncpy(r.collected_at, getCurrentTimeISO().c_str(), 24);
    readAccel(r.accel_x, r.accel_y, r.accel_z);
    batch[batchCount++] = r;
    if(batchCount >= BATCH_SIZE) flushBatchToSD();
  }

  // Upload condicional
  struct tm timeinfo;
  bool podeSincronizar = true;
  if(getLocalTime(&timeinfo)) {
    int hourNow = timeinfo.tm_hour;
    if(hourNow >= 8 && hourNow < 15) {
      podeSincronizar = false;
    }
    if(hourNow >= 21 || hourNow < 3) {
      Serial.println("🌙 Deep Sleep longo (21h-03h)...");
      esp_sleep_enable_timer_wakeup(6*60*60*uS_TO_S_FACTOR);
      if(batchCount > 0) flushBatchToSD();  // salva leituras pendentes
      esp_deep_sleep_start();
    }
  }

  if(podeSincronizar && (now - lastUploadMs >= UPLOAD_INTERVAL_MS)) {
    lastUploadMs = now;
    if(batchCount > 0 && batchCount >= BATCH_SIZE/2) flushBatchToSD();
    tryUploadQueue();
  }

 
  // calcula o tempo até o próximo evento
  uint32_t nextSample = (lastSampleMs + SAMPLE_INTERVAL_MS > now) 
                        ? (lastSampleMs + SAMPLE_INTERVAL_MS - now) : 0;
  uint32_t nextUpload = (lastUploadMs + UPLOAD_INTERVAL_MS > now) 
                        ? (lastUploadMs + UPLOAD_INTERVAL_MS - now) : 0;

  uint32_t sleepMs = min(nextSample, nextUpload);
  if(sleepMs == 0) sleepMs = 5; // fallback de segurança

  esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
  esp_light_sleep_start();
}



