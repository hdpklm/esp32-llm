# Estado del Proyecto - ESP32 LLM

## Descripción
Este proyecto implementa la inferencia de un modelo de lenguaje pequeño (LLM) en un ESP32 (específicamente ESP32-S3 con PSRAM). Utiliza una versión modificada de `llama2.c` optimizada para el hardware de Espressif.

## Arquitectura
- **Núcleo de LLM (`llm.c`/`llm.h`)**: Basado en `llama2.c`, con optimizaciones SIMD usando la librería `esp-dsp` y ejecución multinúcleo.
- **Main (`main.c`)**: Gestiona la inicialización de hardware (Display OLED, almacenamiento SPIFFS) y la interfaz de usuario via UART.
- **Comunicación**: El usuario envía prompts por UART0, y el LLM responde por la misma vía.

## Estado Actual
- Implementada la generación con callback para integración con UART.
- Sistema de exclusión mutua (mutex) para evitar conflictos de generación concurrentes.

## Pendientes / Problemas Detectados
- ~~Tokens duplicados en la salida UART.~~ (Corregido)
- ~~Crash (Guru Meditation Error) al terminar la generación por dereferencia de puntero NULL.~~ (Corregido)
