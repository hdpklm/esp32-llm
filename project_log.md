# Registro del Proyecto - ESP32 LLM

## 📝 Registro: v1.1 - Corrección de salida duplicada y Crash
- **Fallo/Motivo**: El output del LLM mostraba cada carácter duplicado y el sistema crasheaba al finalizar la generación.
- **Causa**: 
    1. Duplicidad: Tanto el callback de UART como el `safe_printf` interno imprimían al mismo terminal UART0.
    2. Crash: Se intentaba llamar a `cb_done` (puntero a función) en `llm.c` sin verificar si era NULL.
- **Solución/Cambio**:
    - Se añadió una verificación `if (cb_done != NULL)` antes de invocar el callback.
    - Se modificó la lógica de impresión de tokens para usar `safe_printf` solo si no hay un callback de token (`on_token == NULL`).

# Backup
(Espacio para ideas descartadas)
