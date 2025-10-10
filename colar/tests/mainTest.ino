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

// se = 1, desabilita sleep
#define DISABLE_SLEEP 1

// ==========================
// ===== ESTRUTURAS ========
// ==========================
//guarda a leitura vindo do ESP32
struct Reading {
  double latitude;
  double longitude;
  float accel_x;
  float accel_y;
  float accel_z;
  char collected_at[25];
};

struct WifiCredential { const char* ssid; const char* pass; };

// ==========================
// ===== CONFIGURAÇÕES ======
// ==========================
WiFiMulti wifiMulti;

// Redes Wi-Fi
WifiCredential WIFI_LIST[] = {
  {"RICARDO", "Paocombanha23*"},
  // {"REDE1", "SENHA1"}
};

// Constantes da API
static const char* API_URL = "https://d8c95e4fdd43.ngrok-free.app/readings";
static const char* API_KEY = "58567df6-233b-4607-971f-d80b6ca927a2";
static const char* API_KEY_HEADER = "Authorization";

//pinos Cs do SD
#define SD_CS_PIN 21
static const char* DIR_QUEUE = "/queue";
static const char* DIR_DEAD = "/deadletter";

// Constantes do MMA7660FC - acelerometro
#define MMA7660_ADDR 0x4C
#define MMA7660_MODE 0x07
#define MMA7660_XOUT 0x00
#define MMA7660_YOUT 0x01
#define MMA7660_ZOUT 0x02

// Intervalos
static const uint32_t SAMPLE_INTERVAL_MS  = 10000;        // 10s para pegar os dados
static const size_t   BATCH_SIZE          = 20;           // 40 leituras = 1 batch
static const uint32_t UPLOAD_INTERVAL_MS  = 5*60*1000;   // 20 min para enviar os dados
//static const long     GMT_OFFSET_SEC      = -3*3600;      // UTC-3 - nao aplicavel - teste
#define uS_TO_S_FACTOR 1000000ULL

// ==========================
// ===== VARIÁVEIS =========
// ==========================
//armazena leituras em RAM até atingir BATCH_SIZE
Reading batch[BATCH_SIZE]; 
size_t batchCount = 0; 
// faz o controle de tempo por millis()
uint32_t lastSampleMs = 0; 
uint32_t lastUploadMs = 0;
//guarda a referencia do ntp para recalculo
time_t lastSyncedTime = 0;
uint32_t lastSyncMillis = 0;
static bool TIME_READY = false; // se true, então tempo sincronizado

// ===============================
// ===== FUNÇÕES ACELEROMETRO ====
// ===============================
bool initAccel() {
  Wire.begin();
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(MMA7660_MODE); // escreve no registro de modo
  Wire.write(0x01); //0x01 ativa o sensor
  return Wire.endTransmission() == 0; // true se sucesso
}

float readAxis(uint8_t axisReg) {
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(axisReg);
  if (Wire.endTransmission(false) != 0) return 0; // pede leitura
  Wire.requestFrom(MMA7660_ADDR, (uint8_t)1, true); // 1 byte
  if (Wire.available()) {
    int8_t val = Wire.read();
    if (val > 31) val -= 64; // ajuste de sinal, acelerometro usa 6 bits, por isso subtrair 64 quando valor maior que 31
    return ((float)val * 1.5) / 32.0; // converte o valor bruto pra g e retorna aceleracao
  }
  return 0;
}
//lendo os eixos x, y, z e atribuindo aos parametros
void readAccel(float &x, float &y, float &z) {
  x = readAxis(MMA7660_XOUT);
  y = readAxis(MMA7660_YOUT);
  z = readAxis(MMA7660_ZOUT);
}

// ==========================
// ===== FUNÇÕES TEMPO ======
// ==========================
//validacao se o NTP foi sincronizado, se maior que 2025
static bool timeIsValid() {
  time_t now = time(nullptr);
  return now >= 1735689600; // >= 2025-01-01
}

//timezone =0, e espera o ntp sincronizar, se fracassou retorna false
static bool syncTimeOnce(uint32_t timeoutMs = 15000) {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t start = millis();
  while (!timeIsValid() && (millis() - start) < timeoutMs) delay(200);
  return timeIsValid();
}
// se tempo valido, armazena para não precisar chamar ntp novamente
static void markTimeReadyIfValid() {
  if (timeIsValid()) {
    lastSyncedTime = time(nullptr);
    lastSyncMillis = millis();
    TIME_READY = true;
  }
}
// sincronizacao do ntp
static bool ensureTimeReady(uint32_t timeoutMs = 15000) {
  if (timeIsValid()) { markTimeReadyIfValid(); return true; }
  if (!WiFi.isConnected()) return false;
  Serial.println("PROCESSING - Sincronizando horário via NTP...");
  if (!syncTimeOnce(timeoutMs)) {
    Serial.println("ERROR - Falha ao obter hora via NTP");
    return false;
  }
  markTimeReadyIfValid();
 // configTime(GMT_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov"); desativando o utc PARA TESTES
  Serial.print("OK - NTP sincronizado: "); Serial.println(isoNowUTC());
  return true;
}
//formata time(nullptr) em "YYYY-MM-DDTHH:MM:SSZ"
String isoNowUTC() {
  time_t now = time(nullptr);
  struct tm tm;
  gmtime_r(&now, &tm);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(buf);
}
//faz lastSyncedTime + (millis()-lastSyncMillis)/1000 para gerar um horario se NTP não for chamado
String getCurrentTimeISO() {
  time_t now;
  if (TIME_READY && lastSyncedTime > 0)
    now = lastSyncedTime + ((millis() - lastSyncMillis)/1000);
  else
    now = time(nullptr);
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
//persiste as pastas se não existirem
void ensureDirs() {
  if (!SD.exists(DIR_QUEUE)) SD.mkdir(DIR_QUEUE);
  if (!SD.exists(DIR_DEAD)) SD.mkdir(DIR_DEAD);
}

//gera nome do arquivo com timestamp e millis para evitar sobrescrita
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

//monsta o json da leitura
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

//se tem leituras, salva no sd em /queue
bool flushBatchToSD() {
  if (batchCount == 0) return true;
  String path = makeBatchFilename();
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.println(" ERROR - Falha ao criar arquivo no SD"); return false; }
  f.print("{\"readings\":[");
  for (size_t i=0; i<batchCount; i++) {
    if (i>0) f.print(",");
    writeReadingJSON(f,batch[i]);
  }
  f.print("]}");
  f.flush();
  f.close();
  Serial.print("OK - Batch salvo: "); Serial.println(path);
  batchCount = 0;
  return true;
}

// ==========================
// ===== FUNÇÕES WIFI =======
// ==========================
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  uint8_t st = wifiMulti.run(); //tenta conectar nas redes definidas em WIFI_LIST
  if (st == WL_CONNECTED) {
    Serial.println("OK - Wi-Fi conectado");
    return true;
  }
  Serial.println("ERROR - Falha Wi-Fi");
  return false;
}

// ==========================
// ===== FUNÇÕES API ========
// ==========================
//abre /queue e percorre os arquivos retornando o primeiro (ordem alfabética) para processamento em ordem
String nextQueueFile() {
  File dir = SD.open(DIR_QUEUE); 
  if (!dir || !dir.isDirectory()) return String();
  String first;
  while (true) {
    File e = dir.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      String name = String(DIR_QUEUE) + "/" + String(e.name());
      if (first.length()==0 || name < first) first = name;
    }
    e.close();
  }
  dir.close();
  return first;
}

//move arquivo para /deadletter
void moveToDeadletter(const String &path) {
  String fname = path.substring(path.lastIndexOf('/')+1);
  String dst = String(DIR_DEAD)+"/"+fname;
  SD.remove(dst);
  SD.rename(path,dst);
}
//envia o arquivo para a API, se sucesso apaga o arquivo, se erro 4xx move para deadletter
bool postFileToAPI(const String &path) {
  File f = SD.open(path, FILE_READ);
  if(!f) return false;

  WiFiClientSecure client; client.setInsecure(); // ignora certificado  e desabilita validacao TLS, lembrar de ajustar para producao
  HTTPClient http;
  if(!http.begin(client, API_URL)) { f.close(); http.end(); return false; }

  http.addHeader("Content-Type","application/json");
  http.addHeader(API_KEY_HEADER, API_KEY);
//faz o POST enviando o conteudo do arquivo aberto
  int httpCode = http.sendRequest("POST",&f,f.size());
  Serial.print("➡️ POST "); Serial.println(httpCode);
  f.close();
  if(httpCode>=200 && httpCode<300) { SD.remove(path); http.end(); return true; } // se retorno 2xx, sucesso e remove o arquivo
  if(httpCode>=400 && httpCode<500) { moveToDeadletter(path); http.end(); return false; } // se 4xx move para deadletter
  http.end(); return false; // outros erros, mantem o arquivo para tentar depois
}

//envia os arquivos da fila, até falhar ou acabar
void tryUploadQueue() {
  if(!ensureWiFi()) return;
  ensureTimeReady(5000);
  while(true) {
    String path = nextQueueFile();
    if(path.length()==0) break;
    if(!postFileToAPI(path)) break;
    delay(100);
  }
  //atualiza o tempo se conseguiu enviar algo
  markTimeReadyIfValid();
#if !DISABLE_SLEEP
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
#endif
}

// ==========================
// ===== SETUP ==============
// ==========================
//configuracao inicial dos processos
void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n=== COLAR ESP32 - MMA7660 + SD Batch ===");

  if(!SD.begin(SD_CS_PIN)) Serial.println("ERROR - Falha ao montar SD");
  else { Serial.println("OK - SD montado"); ensureDirs(); }

  for(auto &w: WIFI_LIST) if(w.ssid && strlen(w.ssid)) wifiMulti.addAP(w.ssid,w.pass); // adiciona as redes ao wifiMulti
  if(!initAccel()) Serial.println("ERROR - Erro acelerômetro"); else Serial.println("OK - Acelerômetro pronto");
//inicializa os timers
  lastSampleMs = millis();
  lastUploadMs = millis();

//tenta conectar wifi e sincronizar ntp
  if (ensureWiFi()) {
    Serial.println("PROCESSING - Tentando sincronizar horário NTP...");
    while (!ensureTimeReady(20000)) {
      Serial.println("PROCESSING Tentando novamente sincronizar NTP...");
      delay(5000);
    }
    Serial.println("OK - NTP sincronizado com sucesso!");
  }
}

// ==========================
// ===== FLUXO PRINCIPAL=====
// ==========================
void loop() {
  uint32_t now = millis();

  if (!TIME_READY) ensureTimeReady(5000);

//horario precisa estar sincronizado para coletar
  if (TIME_READY && (now - lastSampleMs >= SAMPLE_INTERVAL_MS)) {
    lastSampleMs = now; //atualiza o timestamp da ultima colata
    Reading r;//deixa fixo 0.0 pq não tem o GPS
    r.latitude = 0.0;
    r.longitude = 0.0;
  strncpy(r.collected_at, getCurrentTimeISO().c_str(), sizeof(r.collected_at)-1); // gera o collected_at
    r.collected_at[sizeof(r.collected_at)-1] = '\0';//copia para o arquivo
    readAccel(r.accel_x, r.accel_y, r.accel_z);//le acelerometro
    batch[batchCount++] = r; //guarda na ram até BatchSize
    if(batchCount >= BATCH_SIZE) flushBatchToSD(); 
  }
//pega horario local
  struct tm timeinfo;
  bool podeSincronizar = true;
  if(getLocalTime(&timeinfo)) {
    int hourNow = timeinfo.tm_hour;
    // se entre 8 e 15 horas, não sincroniza
    if(hourNow >= 11 && hourNow < 18) podeSincronizar = false; 
  }
// se fora do bloqueio e UPLOAD_INTERVAL_MS passou, tenta enviar
  if(podeSincronizar && (now - lastUploadMs >= UPLOAD_INTERVAL_MS)) {
    lastUploadMs = now;
    if(batchCount > 0 && batchCount >= BATCH_SIZE/2) flushBatchToSD(); // tenta enviar batch incompleto pra aproveitar
    tryUploadQueue();//manda os arquivos da /queue
    markTimeReadyIfValid(); //atualiza
  }

  uint32_t nextSample = (lastSampleMs + SAMPLE_INTERVAL_MS > now) ? (lastSampleMs + SAMPLE_INTERVAL_MS - now) : 0; // calcula tempo para a proxima leitura 
  uint32_t nextUpload = (lastUploadMs + UPLOAD_INTERVAL_MS > now) ? (lastUploadMs + UPLOAD_INTERVAL_MS - now) : 0;// calcula tempo proximo upload
  uint32_t sleepMs = min(nextSample, nextUpload); // escolhe o menor entre os dois
  if(sleepMs == 0) sleepMs = 5; // se der errado, envia dvn em 5ms

#if !DISABLE_SLEEP
  esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
  esp_light_sleep_start();
#else
  delay(sleepMs);
#endif
}


