# ESP32S3 SD Batch Uploader - README

Este projeto é um sistema de coleta e envio de leituras de sensores (GPS e acelerômetro) usando ESP32S3 Sense, com armazenamento em lote no cartão SD e envio resiliente para uma API Rails.

## Funcionalidades

- Coleta periódica de leituras do GPS e acelerômetro.
- Armazenamento em batch no microSD para evitar perda de dados.
- Envio de dados em lote para API via HTTP POST com autenticação por UUID.
- Suporte a múltiplas redes Wi-Fi com reconexão automática.
- Gerenciamento de falhas temporárias e permanentes com pastas `/queue` e `/deadletter`.
- Uso do RTC interno do ESP32S3 para timestamp ISO8601, sincronizado via NTP se disponível.

## Configurações Necessárias

- **API_URL**: URL da API que recebe os dados.
- **API_KEY**: UUID do dispositivo cadastrado na API.
- **WIFI_LIST**: Lista de SSIDs e senhas das redes disponíveis.
- **SD_CS_PIN**: Pino CS do cartão SD (21 no Sense).
- **Parâmetros de batch e intervalos**:
  - `SAMPLE_INTERVAL_MS`: intervalo entre leituras.
  - `BATCH_SIZE`: número máximo de leituras por arquivo.
  - `UPLOAD_INTERVAL_MS`: intervalo entre tentativas de envio.
  - `HTTP_TIMEOUT_MS`: timeout para requisições HTTP.
- **Fuso horário NTP**: `GMT_OFFSET_SEC` e `DST_OFFSET_SEC`.

## É necessário ler:

-**DOC Xiao ESP32C3 COM CONFIGURAÇÕES PARA RODAR O PROJETO**: [ESP32C3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)

-**DOC Acelerometro MMA7660FC**: [MMA7660FC Acelerometer](https://wiki.seeedstudio.com/Grove-3-Axis_Digital_Accelerometer-1.5g/#resources)

-**DOC GPS Air 530**: [Air530](https://wiki.seeedstudio.com/Grove-GPS-Air530/)

-**DOC base para conexão**: [GroveShield](https://wiki.seeedstudio.com/Grove-Shield-for-Seeeduino-XIAO-embedded-battery-management-chip/)


## Cuidados para Funcionamento

1. **Cartão SD**:
   - Formate em FAT32, até 32GB.
   - Insira corretamente antes de ligar o ESP.
   - `SD_CS_PIN` deve estar correto.

2. **Wi-Fi**:
   - Preencha `WIFI_LIST` corretamente.
   - Certifique-se de alcance de sinal.

3. **API**:
   - API acessível na rede local inicialmente.
   - UUID correto em `API_KEY`.

4. **Hora e RTC**:
   - Wi-Fi necessário ao menos uma vez para hora real.
   - Sem Wi-Fi/GPS, relógio mantém contagem relativa.

5. **Pastas e Resiliência**:
   - Pastas `/queue` e `/deadletter` essenciais.
   - Evitar desligar durante escrita no SD.
   - Arquivos 4xx vão para `/deadletter`.

6. **Sensores**:
   - Substituir `generateFakeReading()` por leituras reais.
   - Inicializar sensores corretamente.

7. **Monitoramento**:
   - Use Serial Monitor do Arduino-IDE para acompanhar logs.


## Estrutura de Arquivos

- `DIR_QUEUE` (`/queue`): arquivos de batch pendentes de envio.
- `DIR_DEAD` (`/deadletter`): arquivos que falharam permanentemente.

## Como Funciona

1. **Setup**:
   - Inicializa Serial e SD.
   - Cria pastas se não existirem.
   - Configura redes Wi-Fi e conecta à primeira disponível.
   - Inicializa timers de coleta e envio.

2. **Loop**:
   - A cada `SAMPLE_INTERVAL_MS`, coleta leitura e adiciona ao batch.
   - Se batch cheio, salva no SD.
   - A cada `UPLOAD_INTERVAL_MS`, envia arquivos pendentes da fila para API.
   - Falhas temporárias: reenvia depois.
   - Falhas permanentes (4xx): move para `/deadletter`.

3. **Armazenamento e envio**:
   - Gera nomes de arquivos com timestamp e millis() para garantir unicidade.
   - Converte leituras para JSON antes de salvar no SD.
   - Envia via HTTP POST com headers `Content-Type: application/json` e `Authorization: <UUID>`.


ontem segui com os testes da reserva, encontrei alguns problemas primeiro que a api nao tava tratando corretamente a chegada do movtoestq do nosso lado 

outro que os registros de movimento estavam se multiplicando


1- editado aplicacao id 5126268, nisso, a aplicacaoitem codigo 5134681 foi alterada para 5134687 



o que deu certo até agora:]

reservas estão criando e atualizando corretamente
apagado a reserva se a aplicacaoItem for apagada
transferenciaestoque e transfereniiaestoque item estao gerando as reservas de acordo, com lotes, e estão retornando o movimento corretamente e atualizando o status corretamente 


apagado movtoestq transf 5457067 mas ainda está com status de pendente