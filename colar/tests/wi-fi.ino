#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 


WiFiMulti wifiMulti;

struct WifiCredential { 
  const char* ssid; 
  const char* pass; 
};

WifiCredential WIFI_LIST[] = {
  {"RICARDO", "Paocombanha23*"},
  // {"OutraRede", "SenhaAqui"},
};

static const char* API_URL = "https://5f3b2fd75bc7.ngrok-free.app/readings"; 
static const char* API_KEY = "58567df6-233b-4607-971f-d80b6ca927a2";
static const char* API_KEY_HEADER = "Authorization";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== TESTE WIFI + API ESP32 ===");

  // Adiciona redes WiFi
  for (auto &w : WIFI_LIST) {
    if (w.ssid && strlen(w.ssid)) {
      wifiMulti.addAP(w.ssid, w.pass);
    }
  }

  // Tenta conectar
  Serial.println("Conectando Wi-Fi...");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.print("\n✅ Conectado em ");
  Serial.println(WiFi.SSID());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Faz a requisição
  sendTestRequest();
}

void loop() {
  // nada por enquanto
}

void sendTestRequest() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Sem conexão Wi-Fi");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // ignora certificado SSL (necessário pro ngrok https)

  HTTPClient http;
  Serial.println("🌐 Iniciando requisição...");

  if (http.begin(client, API_URL)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader(API_KEY_HEADER, API_KEY);

    String payload = "{\"teste\": \"ola mundo\"}";
    int httpCode = http.POST(payload);

    Serial.print("➡️ Status: ");
    Serial.println(httpCode);

    if (httpCode > 0) {
      String response = http.getString();
      Serial.print("📩 Resposta: ");
      Serial.println(response);
    } else {
      Serial.print("Erro HTTP: ");
      Serial.println(http.errorToString(httpCode));
    }

    http.end();
  } else {
    Serial.println("❌ Falha em http.begin()");
  }
}
