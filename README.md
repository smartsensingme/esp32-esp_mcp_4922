# Especificação de Projeto: Componente ESP-IDF `esp_mcp4922` e Exemplo Gerador de Senoides

## 1. Visão Geral
Atue como um desenvolvedor Sênior de Sistemas Embarcados especialista em ESP-IDF (v5.x). Seu objetivo é escrever um componente genérico e robusto para o DAC MCP4922 (via SPI) e um projeto de exemplo para o microcontrolador **ESP32-S3**.

O projeto deve seguir estritamente a estrutura de diretórios do ESP-IDF:
- `components/esp_mcp4922/` (com `CMakeLists.txt`, `.c`, `.h`)
- `main/` (com `CMakeLists.txt`, `main.c`)

---

## 2. Especificação do Componente (`esp_mcp4922`)

O componente deve abstrair o controle de múltiplos chips MCP4922 no mesmo barramento SPI. Cada chip possui 2 canais (A e B). O usuário informará a quantidade de **chips**, e o componente gerenciará a quantidade de **canais** internamente (num_canais = num_chips * 2).

### 2.1. Estruturas de Dados
Crie um `struct` de configuração (`mcp4922_config_t`) que receba:
- Host SPI (ex: `SPI2_HOST`).
- Pinos: MOSI, SCK, LDAC.
- Número de chips (`num_chips`).
- Um ponteiro para um array de pinos CS (`gpio_num_t *cs_pins`), onde o tamanho é igual a `num_chips`.
- Configuração global de ganho (1x ou 2x) e VREF buffer.

Crie um `struct` de contexto (`mcp4922_context_t`) que será retornado/preenchido pelo inicializador, contendo as *handles* dos dispositivos SPI registrados.

### 2.2. API Requerida
A biblioteca deve fornecer 3 funções principais:

1. **`esp_err_t mcp4922_init(...)`**
   - Configura o barramento SPI (Modo 0, MSB first, até 20MHz).
   - Registra um *device* no barramento para cada pino CS fornecido.
   - Configura o pino LDAC como saída GPIO comum.

2. **`esp_err_t mcp4922_write_channels(mcp4922_context_t *ctx, uint16_t *channel_values)`**
   - Função para uso em **Task normal** (Flash/FreeRTOS).
   - Usa `spi_device_transmit` (bloqueante, com semáforo).
   - Monta o *frame* de 16 bits (4 bits de config + 12 bits de dados) on-the-fly.
   - Itera sobre os canais: índice par = Canal A, índice ímpar = Canal B. A cada 2 canais, avança para a próxima *handle* (próximo CS).
   - Ao final do loop, gera um pulso (LOW -> HIGH) no pino LDAC.

3. **`void IRAM_ATTR mcp4922_write_channels_isr(mcp4922_context_t *ctx, uint16_t *channel_values)`**
   - Função para uso **EXCLUSIVO em Interrupções de Hardware**.
   - MUST USE `IRAM_ATTR` na declaração e implementação.
   - Usa exclusivamente `spi_device_polling_transmit` (sem semáforos, sem bloqueio de kernel).
   - Mesma lógica de iteração e pulso LDAC da função acima, mas otimizada para velocidade extrema.

---

## 3. Especificação do Exemplo (`main.c`)

O exemplo deve demonstrar a capacidade de tempo real do ESP32-S3 e do componente. O objetivo é gerar **3 senoides de 60 Hz, defasadas em 90 graus**, utilizando 2 chips MCP4922 (ou seja, 4 canais disponíveis, usaremos apenas os índices 0, 1 e 2 do array).

### 3.1. Requisitos do Sistema
- **Microcontrolador:** ESP32-S3.
- **Pinos para o Exemplo:** MOSI = 11, SCK = 12, CS1 = 10, CS2 = 9, LDAC = 14.
- **Frequência de Atualização:** 13.500 Hz (13.5 kHz).
- **Look-Up Table (LUT):** Crie um array estático (`IRAM_ATTR`) de 225 posições (`uint16_t`). 
  - *Cálculo:* 13500 Hz / 60 Hz = 225 pontos por ciclo.
  - A função `app_main` deve calcular a senoide (de 0 a 4095, centrada em 2048) e popular esse array antes de iniciar o timer.

### 3.2. Configuração do Timer (GPTimer)
- Configure um General Purpose Timer (GPTimer) para disparar um alarme a cada **~74.07 microssegundos** (frequência de 13.5 kHz).
- **CRÍTICO:** A interrupção (ISR) do timer **DEVE** ser alocada obrigatoriamente no **CORE 1** do ESP32-S3 (use flags de alocação de interrupção ou crie a task de inicialização do timer pinada no Core 1). Isso evita conflitos com o rádio Wi-Fi no Core 0.

### 3.3. Rotina de Interrupção (ISR)
- A callback do timer (`IRAM_ATTR`) manterá um contador estático `idx` (de 0 a 224).
- Deve criar um array local `uint16_t val[4]`.
- Mapeamento das Fases (90 graus de defasagem = 1/4 do ciclo de 225 = ~56 índices de offset):
  - `val[0] = lut[idx];` (Seno 1 - 0º)
  - `val[1] = lut[(idx + 56) % 225];` (Seno 2 - 90º)
  - `val[2] = lut[(idx + 112) % 225];` (Seno 3 - 180º)
  - `val[3] = 0;` (Canal não utilizado)
- Chamar `mcp4922_write_channels_isr(&ctx, val);`.
- Incrementar `idx` (com wrap-around em 225).

---
**Nota para a IA:** Gere o código completo e os arquivos `CMakeLists.txt` compatíveis com o build system do ESP-IDF. Documente o código C com clareza.
