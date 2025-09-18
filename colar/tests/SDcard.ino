#include "FS.h"
#include "SD.h"
#include "SPI.h"

void setup() {
  Serial.begin(115200);
  while(!Serial);

  // Inicializa o cartao no CS = GPIO21
  if (!SD.begin(21)) {
    Serial.println("Falha ao montar o cartao SD!");
    return;
  }

  Serial.println("cartao SD montado!");

  // Mostra informações do cartao
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("Nenhum cartao detectado.");
    return;
  }

  Serial.print("Tipo de cartao: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("Desconhecido");

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("Tamanho do cartao: %llu MB\n", cardSize);

  // Cria arquivo e escreve
  File file = SD.open("/test.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Erro ao abrir arquivo para escrita");
    return;
  }
  file.println("Hello World from ESP32S3! e algumas asneiras...");
  file.close();
  Serial.println("Arquivo escrito: /test.txt");

  // Lê de volta o conteudo
  file = SD.open("/test.txt");
  if (!file) {
    Serial.println("Erro ao abrir arquivo para leitura");
    return;
  }

  Serial.println("Conteudo de /test.txt:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void loop() {
}
