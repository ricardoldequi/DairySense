#include <Wire.h>

// Endereço I2C do MMA7660FC
#define MMA7660_ADDR 0x4C  

// Registradores do sensor
#define MMA7660_XOUT 0x00
#define MMA7660_YOUT 0x01
#define MMA7660_ZOUT 0x02
#define MMA7660_MODE 0x07

// Sensibilidade (21 LSB = 1g)
#define MMA7660_SENSITIVITY 21.0  

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Configura o sensor no modo ativo
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(MMA7660_MODE);
  Wire.write(0x01); // Ativar
  Wire.endTransmission();

  delay(100);

  Serial.println("Teste do Acelerômetro MMA7660FC iniciado");
}

void loop() {
  int8_t x, y, z;

  // Ler X
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(MMA7660_XOUT);
  Wire.endTransmission(false);
  Wire.requestFrom(MMA7660_ADDR, 1);
  if (Wire.available()) x = (int8_t)(Wire.read() << 2) >> 2; 

  // Ler Y
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(MMA7660_YOUT);
  Wire.endTransmission(false);
  Wire.requestFrom(MMA7660_ADDR, 1);
  if (Wire.available()) y = (int8_t)(Wire.read() << 2) >> 2;

  // Ler Z
  Wire.beginTransmission(MMA7660_ADDR);
  Wire.write(MMA7660_ZOUT);
  Wire.endTransmission(false);
  Wire.requestFrom(MMA7660_ADDR, 1);
  if (Wire.available()) z = (int8_t)(Wire.read() << 2) >> 2;

  // Converter para 'g'
  float ax = x / MMA7660_SENSITIVITY;
  float ay = y / MMA7660_SENSITIVITY;
  float az = z / MMA7660_SENSITIVITY;

  // Mostrar resultados
  Serial.print("X: "); Serial.print(ax, 2);
  Serial.print(" g | Y: "); Serial.print(ay, 2);
  Serial.print(" g | Z: "); Serial.print(az, 2);
  Serial.println(" g");

  delay(500);
}