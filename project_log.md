# Registro del Proyecto - ESP32 LLM

## 📝 Registro: v1.1 - Corrección de salida duplicada y Crash
- **Fallo/Motivo**: El output del LLM mostraba cada carácter duplicado y el sistema crasheaba al finalizar la generación.
- **Causa**: 
    1. Duplicidad: Tanto el callback de UART como el `safe_printf` interno imprimían al mismo terminal UART0.
    2. Crash: Se intentaba llamar a `cb_done` (puntero a función) en `llm.c` sin verificar si era NULL.
- **Solución/Cambio**:
    - Se añadió una verificación `if (cb_done != NULL)` antes de invocar el callback.
    - Se modificó la lógica de impresión de tokens para usar `safe_printf` solo si no hay un callback de token (`on_token == NULL`).

## 📝 Registro: v1.2 - Implementación de Echo y Backspace en UART
- **Fallo/Motivo**: El usuario no podía ver lo que escribía en el terminal ni corregir errores (backspace).
- **Causa**: La tarea de UART leía los bytes pero no los devolvía (echo) ni procesaba caracteres especiales de control.
- **Solución/Cambio**:
    - Se añadió eco de caracteres en `uart_llm_task_2`.
    - Se implementó el manejo de `0x08` (BS) y `0x7F` (DEL) para retroceder el índice del buffer y enviar la secuencia de escape `"\b \b"` para borrar visualmente en el terminal.
    - Se añadió un prompt visual `> ` al terminar la generación.

# Backup
(Espacio para ideas descartadas)
