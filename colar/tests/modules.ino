#include <Wire.h>
#include <MMA7660.h>
#include <TinyGPSPlus.h>

// === MMA7660 (Acelerômetro) ===
MMA7660 mma;

// === GPS Air530 (UART1 no ESP32-S3) ===
HardwareSerial GPS(1);
TinyGPSPlus gps;

// Pinos do GPS no XIAO ESP32S3
#define GPS_RX 44  // RX do ESP32 (conectar no TX do GPS)
#define GPS_TX 43  // TX do ESP32 (conectar no RX do GPS)

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("===== Teste Unificado: MMA7660 + Air530 GPS =====");

  // --- Inicializa MMA7660 ---
  mma.init();
  Serial.println("MMA7660 OK.");

  // --- Inicializa GPS ---
  GPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS inicializado (9600 bps). Aguardando sinais...");
}

void loop() {
  // === Leitura do MMA7660 ===
  int8_t x, y, z;
  mma.getXYZ(&x, &y, &z);
  Serial.print("Acelerômetro MMA7660 - X: ");
  Serial.print(x);
  Serial.print(" Y: ");
  Serial.print(y);
  Serial.print(" Z: ");
  Serial.println(z);

  // === Leitura do GPS ===
  while (GPS.available()) {
    char c = GPS.read();
    gps.encode(c);
  }

  if (gps.location.isUpdated()) {
    Serial.print("GPS - Lat: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(" Lon: ");
    Serial.print(gps.location.lng(), 6);
    Serial.print(" Satélites: ");
    Serial.print(gps.satellites.value());
    Serial.print(" HDOP: ");
    Serial.println(gps.hdop.hdop());
  } else {
    Serial.println("GPS - aguardando fix...");
  }

  Serial.println("--------------------------------------------------");
  delay(2000);
}
