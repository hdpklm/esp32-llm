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

## 📝 Registro: v1.3 - Sistema de Comandos y Anclaje Estricto
- **Fallo/Motivo**: Necesidad de configurar parámetros y evitar falsos positivos de comandos dentro del texto del prompt.
- **Causa**: Mejora en la robustez del parser de comandos.
- **Solución/Cambio**:
    - Los comandos (`/h`, `/s`, `/t`, `/l`) ahora solo se ejecutan si el `/` es el **primer carácter absoluto** de la línea.
    - Si el `/` aparece en cualquier otra posición, se trata como texto normal.
    - Si se escribe algo como `/x` (comando desconocido) al inicio, el sistema ahora lo envía íntegramente al LLM en lugar de dar error, permitiendo prompts que empiecen por slash.
    - Se mantiene `//` al inicio como escape para enviar un `/` literal.

## 📝 Registro: v1.4 - Workaround para Error de Compilación (ICE)
- **Fallo/Motivo**: El compilador GCC crasheaba (`Internal Compiler Error: Segmentation fault`) al intentar compilar el componente `esp-dsp`, específicamente en la fase `ira`.
- **Causa**: Un bug conocido en ciertas versiones del toolchain de Xtensa al aplicar optimizaciones agresivas en bucles complejos de DSP.
- **Solución/Cambio**:
    - Se modificó `main/CMakeLists.txt` para aplicar flags de desactivación de optimizaciones problemáticas al componente `espressif__esp-dsp`.
    - Flags aplicadas: `-fno-if-conversion`, `-fno-ira-share-save-slots` y `-fno-ira-share-spill-slots`.
    - Estas flags evitan que el compilador intente las transformaciones que provocan el segfault sin sacrificar significativamente el rendimiento del resto del sistema.

## 📝 Registro: v1.5 - Corrección de Crash (Overflow) y Tipos de Datos
- **Fallo/Motivo**: El sistema crasheaba con un `assert` en `xQueueGenericSend` al aumentar la longitud de generación (`/l 1024`) y mostraba valores erróneos en el comando `/s`.
- **Causa**: 
    1. **Overflow**: Intentar generar más tokens que el `seq_len` del modelo provocaba un desbordamiento del buffer de la Cache KV en el heap, corrompiendo estructuras de datos de FreeRTOS.
    2. **Corrupción de Parámetros**: Desajuste entre el prototipo de `build_sampler` en `llm.h` (`float`) y su implementación en `llm.c` (`v4sf`). Al ser `v4sf` un tipo alineado a 16 bytes, el paso de parámetros por valor fallaba.
- **Solución/Cambio**:
    - Se añadió un límite estricto en el comando `/l` y en la función `generate` para no exceder `transformer.config.seq_len`.
    - Se corrigió la firma de `build_sampler` en `llm.c` para usar `float` estándar para los parámetros escalares.
    - Se mejoró el comando `/s` para mostrar el límite máximo admitido por el modelo cargado.

## 📝 Registro: v1.6 - Corrección de Crash por Alineación (SIMD)
- **Fallo/Motivo**: El sistema crasheaba con `LoadProhibited` (Core 0) al iniciar la primera generación.
- **Causa**: Las instrucciones SIMD de ESP-DSP (`dsps_dotprod_f32_aes3`) requieren que los datos estén alineados a 16 bytes. Tanto el buffer de pesos (que empezaba en un offset de 28 bytes) como los buffers de `RunState` (asignados con `malloc`/`calloc` estándar) no garantizaban esta alineación.
- **Solución/Cambio**:
    - Se implementó `malloc_aligned` usando `heap_caps_aligned_alloc(16, ...)` para todos los buffers de activación en `llm.c`.
    - Se modificó `read_checkpoint` para leer los pesos en un buffer alineado de 16 bytes, separando la cabecera `Config` del inicio del buffer de datos.
    - Se aumentó el tamaño de stack de las tareas `MatMul2` y `ForwardTask` de 2048 a 4096 bytes para mayor seguridad.

## 📝 Registro: v1.7 - Corrección Final de Alineación y offsets
- **Fallo/Motivo**: Salida incoherente (garbage) y crash por corrupción de heap.
- **Causa**: 
    1. **v4sf Size**: La definición de `v4sf` con alineación forzada en el `typedef` aumentaba su tamaño a 16 bytes, rompiendo toda la aritmética de punteros para pesos que son floats de 4 bytes.
    2. **Read Offset**: En `v1.6` se reseteba el puntero del archivo a 0 antes de leer los pesos, incluyendo la cabecera `Config` en el buffer de pesos y desplazando todo 28 bytes.
- **Solución/Cambio**:
    - Se cambió `v4sf` a un simple `float`. La alineación SIMD se garantiza en el punto de reserva de memoria (`malloc_aligned`).
    - Se corrigió `read_checkpoint` para saltar exactamente `sizeof(Config)` bytes antes de leer los pesos en el buffer alineado.
    - Se simplificó la lógica de lectura asegurando que el buffer de datos solo contenga los pesos y esté alineado a 16.

## 📝 Registro: v1.8 - Reajuste de Lógica de Longitud (Steps)
- **Fallo/Motivo**: El usuario observó que al configurar `/l 15`, la generación se cortaba prematuramente si el prompt era largo, ya que los "steps" se contaban como (entrada + salida).
- **Causa**: La lógica del loop principal en `generate` usaba los "steps" como el límite absoluto de la posición (`pos`), incluyendo los tokens del prompt.
- **Solución/Cambio**:
    - Se modificó `llm.c` para que `steps` represente exclusivamente la cantidad de tokens a **generar**.
    - El nuevo límite del loop es `total_steps = num_prompt_tokens + steps`.
    - Se mantiene la protección para que `total_steps` nunca exceda el `seq_len` (ventana de contexto) del modelo.

# Backup
(Espacio para ideas descartadas)
