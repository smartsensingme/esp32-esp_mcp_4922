# Componente DAC de Alta Performance ESP-MCP4922

*Leia em outros idiomas: [English](README.md)*

Um componente ESP-IDF de latência ultrabaixa e otimizado em bare-metal para o DAC duplo de 12 bits **Microchip MCP4922**.

Este componente foi projetado especificamente para **laços de controle digital de tempo real rígido (hard real-time)** (como controladores PI/PID de alta frequência para eletrônica de potência ou inversores), onde a sobrecarga padrão do RTOS e dos drivers SPI é inaceitável.

## Características

- **Operação em Modo Duplo**:
  - **Modo Padrão (`mcp4922_write_channels`)**: Implementação padrão e *thread-safe* do driver ESP-IDF. Ideal quando o barramento SPI é compartilhado com outros periféricos (como cartões SD ou displays TFT).
  - **Modo Bare-Metal (`mcp4922_ll_write_channels`)**: Ignora a camada de abstração do ESP-IDF e escreve diretamente no FIFO de hardware do SPI (`GPSPI2.data_buf`). Elimina os bloqueios do FreeRTOS e verificações de contexto, reduzindo a latência de transmissão de >100us para **~3us**.
- **Suporte a Múltiplos Chips**: Controle facilmente vários chips MCP4922 no mesmo barramento SPI multiplexando o CS por software.
- **Multiplataforma**: A implementação Bare-Metal usa macros de pré-processador para suportar perfeitamente o ESP32 clássico, bem como chips modernos (ESP32-S2, S3, C3, C6 e ESP32-P4).

## Estrutura do Projeto

Este projeto está estruturado como um componente ESP-IDF com um exemplo de uso incluído:

* `components/esp_mcp4922/`: O componente central do driver. Contém as implementações de alto e baixo nível.
* `main/main.c`: Um exemplo completo de uso. Ele configura o DAC e utiliza o `gptimer` do ESP-IDF para gerar uma perfeita **onda senoidal trifásica de 60Hz (defasagem de 120°)** com uma taxa de amostragem de 13,5kHz.

## Exemplo de Uso: Gerador de Onda Senoidal Trifásica

O exemplo fornecido em `main.c` demonstra o padrão RTOS "Processamento Diferido de Interrupção" (Deferred Interrupt Processing). Ele usa um timer de hardware de alta frequência (13,5kHz) para acordar uma tarefa FreeRTOS de prioridade máxima. Isso garante um determinismo de tempo enquanto mantém a ISR (Rotina de Serviço de Interrupção) limpa, evitando a corrupção de contexto da FPU que ocorreria se a matemática de ponto flutuante fosse usada dentro da própria ISR.

### Configuração de Hardware

Por padrão, o componente usa os seguintes pinos (configuráveis via `menuconfig` do ESP-IDF):

* **MOSI**: GPIO 11
* **SCK**: GPIO 12
* **LDAC**: GPIO 14
* **CS1** (Chip 1): GPIO 10
* **CS2** (Chip 2): GPIO 9

*Nota: O pino CS de hardware do periférico SPI é intencionalmente desativado (`spics_io_num = -1`). O componente controla manualmente os pinos CS usando comandos rápidos `gpio_ll` para eliminar a enorme sobrecarga do driver causada pela rápida troca de dispositivos.*

### Como Compilar e Rodar

1. Certifique-se de que o seu ambiente ESP-IDF esteja ativo (`. export.sh`).
2. Compile o projeto:
   ```bash
   idf.py build
   ```
3. Grave e monitore:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

## Filosofia de Design e Arquitetura

Para um laço de controle digital rodando a 13,5kHz, a janela de execução é de exatamente **74 microssegundos**. Transações SPI padrão com mutexes do FreeRTOS levam cerca de 25us por chamada. Atualizar 4 canais (2 chips) levaria >100us, o que mataria a Idle Task de fome e dispararia o Watchdog de Tarefas do FreeRTOS (TWDT).

Ao adquirir o barramento SPI permanentemente (`spi_device_acquire_bus`) e utilizar a função `mcp4922_ll_write_channels`, o tempo de execução é reduzido para < 4us. Isso deixa ~70us de tempo livre de CPU para a pesada lógica de controle em ponto flutuante (PI/PID), enquanto neutraliza completamente os travamentos por Watchdog.

---
![SmartSensing.me Logo](https://smartsensing.me/ssme-logo.png)

## 📝 Descrição

Este projeto faz parte do ecossistema **SmartSensing.me** e vai além dos exemplos básicos encontrados na internet. Aqui, aplicamos os fundamentos reais da engenharia de instrumentação e sistemas embarcados de alta performance.

Diferente de conteúdos superficiais voltados apenas para cliques, este repositório entrega:
- **Ineditismo:** Implementações originais baseadas em quase 30 anos de experiência acadêmica.
- **Densidade Técnica:** Uso profissional do framework ESP-IDF e FreeRTOS.
- **Didática:** Código documentado e estruturado para quem busca evolução técnica real.

> "Transformamos sinais do mundo físico em inteligência digital, sem atalhos."

---

## 🛠️ Tecnologias
- **Hardware Target:** ESP32 / ESP32-S3
- **Framework:** ESP-IDF v5.x
- **Linguagem:** C / C++
- **Simulação:** LTSpice (Modelagem de Sensores)

---

## 👤 Sobre o Autor

**José Alexandre de França** *Professor Adjunto no Departamento de Engenharia Elétrica da UEL*

Engenheiro Eletricista com quase três décadas de experiência no ensino de graduação e pós-graduação. Doutor em Engenharia Elétrica, pesquisador em instrumentação eletrônica e desenvolvedor de sistemas embarcados. O SmartSensing.me é o meu compromisso de elevar o nível da educação tecnológica no Brasil.

- 🌐 **Website:** [smartsensing.me](https://smartsensing.me)
- 📧 **E-mail:** [info@smartsensing.me](mailto:info@smartsensing.me)
- 📺 **YouTube:** [@smartsensingme](https://youtube.com/@smartsensingme)
- 📸 **Instagram:** [@smartsensing.me](https://instagram.com/smartsensing.me)

---

## 📄 Licença

Este projeto está sob a licença MIT. Veja o arquivo [LICENSE](LICENSE) para detalhes.
