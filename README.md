# ESP-MCP4922 High-Performance DAC Component

*Read this in other languages: [Português](README.pt-br.md)*

An ultra-low latency, bare-metal optimized ESP-IDF component for the **Microchip MCP4922** 12-bit Dual DAC. 

This component is specifically engineered for **hard real-time digital control loops** (such as high-frequency PI/PID controllers for power electronics or inverters), where standard RTOS and SPI driver overheads are unacceptable.

## Features

- **Dual-Mode Operation**:
  - **Standard Mode (`mcp4922_write_channels`)**: Thread-safe, standard ESP-IDF driver implementation. Ideal when the SPI bus is shared with other peripherals (like SD cards or TFT displays).
  - **Bare-Metal Mode (`mcp4922_ll_write_channels`)**: Bypasses the ESP-IDF abstraction layer and writes directly to the physical SPI hardware FIFO (`GPSPI2.data_buf`). Eliminates FreeRTOS locks and context checks, reducing transmission latency from >100us to **~3us**.
- **Multi-Chip Support**: Easily control multiple MCP4922 chips on the same SPI bus with software CS multiplexing.
- **Cross-Platform**: The Bare-Metal implementation uses preprocessor macros to seamlessly support the classic ESP32, as well as modern chips (ESP32-S2, S3, C3, C6, and ESP32-P4).

## Project Structure

This project is structured as an ESP-IDF component with an included usage example:

* `components/esp_mcp4922/`: The core driver component. Contains the high-level and low-level implementations.
* `main/main.c`: A complete usage example. It configures the DAC and utilizes the ESP-IDF `gptimer` to generate a perfect **3-phase 60Hz sine wave (120° offset)** at a 13.5kHz sampling rate.

## Example Usage: 3-Phase Sine Wave Generator

The example provided in `main.c` demonstrates the "Deferred Interrupt Processing" RTOS pattern. It uses a high-frequency (13.5kHz) hardware timer to wake up a maximum-priority FreeRTOS task. This ensures deterministic timing while keeping the ISR clean, preventing FPU context corruption that would occur if floating-point math were used inside the ISR itself.

### Hardware Setup

By default, the component uses the following pins (configurable via ESP-IDF `menuconfig`):

* **MOSI**: GPIO 11
* **SCK**: GPIO 12
* **LDAC**: GPIO 14
* **CS1** (Chip 1): GPIO 10
* **CS2** (Chip 2): GPIO 9

*Note: The hardware CS pin of the SPI peripheral is intentionally disabled (`spics_io_num = -1`). The component manually drives the CS pins using fast `gpio_ll` commands to eliminate the massive driver overhead caused by rapid device switching.*

### How to Build and Run

1. Make sure your ESP-IDF environment is active (`. export.sh`).
2. Build the project:
   ```bash
   idf.py build
   ```
3. Flash and monitor:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

## Design Philosophy & Architecture

For a digital control loop running at 13.5kHz, the execution window is exactly **74 microseconds**. Standard SPI transactions with FreeRTOS mutexes take around 25us per call. Updating 4 channels (2 chips) would take >100us, starving the Idle Task and triggering the FreeRTOS Task Watchdog (TWDT).

By acquiring the SPI bus permanently (`spi_device_acquire_bus`) and utilizing the `mcp4922_ll_write_channels` function, the execution time is reduced to < 4us. This leaves ~70us of free CPU time for heavy floating-point control logic (PI/PID) while completely neutralizing Watchdog crashes.

---
![SmartSensing.me Logo](https://smartsensing.me/ssme-logo.png)

## 📝 Description

This project is part of the **SmartSensing.me** ecosystem and goes beyond basic examples found on the internet. Here, we apply the real fundamentals of high-performance instrumentation and embedded systems engineering.

Unlike superficial content aimed only at clicks, this repository delivers:
- **Originality:** Original implementations based on nearly 30 years of academic experience.
- **Technical Density:** Professional use of the ESP-IDF framework and FreeRTOS.
- **Didactics:** Documented and structured code for those seeking real technical evolution.

> "We transform signals from the physical world into digital intelligence, without shortcuts."

---

## 🛠️ Technologies
- **Hardware Target:** ESP32 / ESP32-S3
- **Framework:** ESP-IDF v5.x
- **Language:** C / C++
- **Simulation:** LTSpice (Sensor Modeling)

---

## 👤 About the Author

**José Alexandre de França** *Adjunct Professor at the Electrical Engineering Department of UEL*

Electrical Engineer with nearly three decades of experience in undergraduate and graduate teaching. PhD in Electrical Engineering, researcher in electronic instrumentation and embedded systems developer. SmartSensing.me is my commitment to raising the level of technological education in Brazil.

- 🌐 **Website:** [smartsensing.me](https://smartsensing.me)
- 📧 **E-mail:** [info@smartsensing.me](mailto:info@smartsensing.me)
- 📺 **YouTube:** [@smartsensingme](https://youtube.com/@smartsensingme)
- 📸 **Instagram:** [@smartsensing.me](https://instagram.com/smartsensing.me)

---

## 📄 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.