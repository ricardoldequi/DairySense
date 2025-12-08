#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 

// Substitua pelo certificado RAIZ que assina o cert do seu domínio (último bloco do chain.pem)
static const char ROOT_CA[] PROGMEM = R"pem(
-----BEGIN CERTIFICATE-----
[CERTIFICADO_CA_RAIZ]
-----END CERTIFICATE-----
)pem";

WiFiMulti wifiMulti;

struct WifiCredential { 
  const char* ssid; 
  const char* pass; 
};

WifiCredential WIFI_LIST[] = {
  {"Ricardo", "dequi2014"},
  // {"OutraRede", "SenhaAqui"},
};

static const char* API_URL = "https://dairysense.com.br/api/readings"; 
static const char* API_KEY = "58567df6-233b-4607-971f-d80b6ca927a2";
static const char* API_KEY_HEADER = "Authorization";

// -- util: aguarda NTP para validação TLS
static void waitForTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  int retries = 0;
  while ((now = time(nullptr)) < 8 * 3600 * 2 && retries < 30) { // ~16h desde epoch
    delay(500);
    retries++;
  }
}

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

  // Ajusta tempo para TLS
  waitForTime();

  // Teste 1: Authorization: <API_KEY>
  sendReadingsRequest(/*useBearer=*/false);

  // Teste 2: Authorization: Bearer <API_KEY>
  sendReadingsRequest(/*useBearer=*/true);
}

void loop() {
  // nada por enquanto
}

// Monta um ISO8601 em UTC (para collected_at)
static String nowIso8601Utc() {
  time_t now = time(nullptr);
  struct tm t;
  gmtime_r(&now, &t);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}

void sendReadingsRequest(bool useBearer) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Sem conexão Wi-Fi");
    return;
  }

  WiFiClientSecure client;
  // TLS com CA (não usar setInsecure em produção)
  client.setCACert(ROOT_CA);

  HTTPClient http;
  Serial.println(useBearer ? "🌐 Enviando com Authorization: Bearer <API_KEY>" 
                           : "🌐 Enviando com Authorization: <API_KEY>");

  if (http.begin(client, API_URL)) {
    http.addHeader("Content-Type", "application/json");

    String authValue = useBearer ? String("Bearer ") + API_KEY : String(API_KEY);
    http.addHeader(API_KEY_HEADER, authValue);

    // Payload válido para sua API
    String payload = String("{\"readings\":[{")
                   + "\"latitude\":-23.55,"
                   + "\"longitude\":-46.64,"
                   + "\"accel_x\":0.1,"
                   + "\"accel_y\":0.2,"
                   + "\"accel_z\":0.3,"
                   + "\"collected_at\":\"" + nowIso8601Utc() + "\""
                   + "}]}";

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
