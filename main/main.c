// #define DISPLAY_OLED

#include <stdio.h>
#include <inttypes.h>
#include "esp_spiffs.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include <time.h>
#include "llm.h"
#include <string.h>
#include "llama.h"


#ifdef DISPLAY_OLED
	#include <u8g2.h>
	#include "u8g2_esp32_hal.h"
	#include <driver/i2c.h>
	u8g2_t u8g2;
#endif

static const char *TAG = "MAIN";

#define PIN_SDA 8
#define PIN_SCL 9
#define OLED_I2C_ADDRESS 0x78

// chatgpt v1
#include "driver/uart.h"
#include <string.h>
void uart_init_custom();
void uart_llm_task(void *pvParameters);
void uart_llm_loop(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler);
int generate_with_output(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler, char *prompt, char *out_buf, size_t out_buf_len);
typedef struct {
    Transformer *transformer;
    Tokenizer *tokenizer;
    Sampler *sampler;
} uart_llm_args_t;
// end v1

// chatgpt v2
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include <string.h>

#define UART_BUF_SIZE 2048
#define UART_PORT UART_NUM_0
#define UART_BAUD 115200

extern Transformer transformer;
extern Tokenizer tokenizer;
extern Sampler sampler;

Transformer transformer;
Tokenizer tokenizer;
Sampler sampler;
extern int generate_with_callback(Transformer *, Tokenizer *, Sampler *, const char *, void (*)(const char*));

SemaphoreHandle_t llm_mutex;
static void uart_llm_task_2(void *arg);
void start_uart_llm_task();
// end v2


/**
 * @brief Configure SSD1306 display
 * Uses I2C connection
 */
void init_display(void) {
	#ifdef DISPLAY_OLED
	    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
	    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
	    u8g2_esp32_hal_init(u8g2_esp32_hal);
	    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
	        &u8g2, U8G2_R0,
	        // u8x8_byte_sw_i2c,
	        u8g2_esp32_i2c_byte_cb,
	        u8g2_esp32_gpio_and_delay_cb); // init u8g2 structure
	    // 0x3c
	    u8x8_SetI2CAddress(&u8g2.u8x8, OLED_I2C_ADDRESS);
	    u8g2_InitDisplay(&u8g2);     // send init sequence to the display, display is in
	                                 // sleep mode after this,
	    u8g2_SetPowerSave(&u8g2, 0); // wake up display
	    u8g2_ClearBuffer(&u8g2);
	    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
	    u8g2_SendBuffer(&u8g2);
	    ESP_LOGI(TAG, "Display initialized");
	#endif
}

/**
 * @brief intializes SPIFFS storage
 * 
 */
void init_storage(void) {

    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
}

/**
 * @brief Outputs to display
 * 
 * @param text The text to output
 */
void write_display(char *text) {
	#ifdef DISPLAY_OLED
	    u8g2_ClearBuffer(&u8g2);
	    u8g2_DrawStr(&u8g2, 0, u8g2_GetDisplayHeight(&u8g2) / 2, text);
	    u8g2_SendBuffer(&u8g2);
	#endif
}

/**
 * @brief Callbacks once generation is done
 * 
 * @param tk_s The number of tokens per second generated
 */
void generate_complete_cb(float tk_s) {
    char buffer[50];
    sprintf(buffer, "%.2f tok/s", tk_s);
    write_display(buffer);
}

/**
 * @brief Draws a llama onscreen
 * 
 */
void draw_llama(void) {
	#ifdef DISPLAY_OLED
	    u8g2_DrawXBM(&u8g2, 0, 0, u8g2_GetDisplayWidth(&u8g2), u8g2_GetDisplayHeight(&u8g2), &llama_bmp);
	    u8g2_SendBuffer(&u8g2);
	#endif
}

void app_main(void) {
    init_display();
    write_display("Loading Model");
    init_storage();

    // default parameters
    char *checkpoint_path = "/data/stories260K.bin"; // e.g. out/model.bin
    char *tokenizer_path = "/data/tok512.bin";
    float temperature = 1.0f;        // 0.0 = greedy deterministic. 1.0 = original. don't set higher
    float topp = 0.9f;               // top-p in nucleus sampling. 1.0 = off. 0.9 works well, but slower
    int steps = 256;                 // number of steps to run for
    char *prompt = NULL;             // prompt string
    unsigned long long rng_seed = 0; // seed rng with time by default

    // parameter validation/overrides
    if (rng_seed <= 0)
        rng_seed = (unsigned int)time(NULL);

    // build the Transformer via the model .bin file
    // Transformer transformer;
    ESP_LOGI(TAG, "LLM Path is %s", checkpoint_path);
    build_transformer(&transformer, checkpoint_path);
    if (steps == 0 || steps > transformer.config.seq_len)
        steps = transformer.config.seq_len; // override to ~max length

    // build the Tokenizer via the tokenizer .bin file
    // Tokenizer tokenizer;
    build_tokenizer(&tokenizer, tokenizer_path, transformer.config.vocab_size);

    // build the Sampler
    // Sampler sampler;
    build_sampler(&sampler, transformer.config.vocab_size, temperature, topp, rng_seed);

    // run!
    draw_llama();
    generate(&transformer, &tokenizer, &sampler, prompt, steps, &generate_complete_cb, NULL);

    // Inicia la tarea UART
    start_uart_llm_task();
}


// chatgpt v1
#define BUF_SIZE 2048
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD 115200

void uart_init_custom() {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
}

void uart_llm_task(void *pvParameters) {
    uart_llm_args_t *args = (uart_llm_args_t*) pvParameters;
    uart_llm_loop(args->transformer, args->tokenizer, args->sampler);
    vTaskDelete(NULL);
}

void uart_llm_loop(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler) {
    char line[BUF_SIZE];
    int pos = 0;

    uint8_t byte;
    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (byte == '\n' || pos >= BUF_SIZE - 1) {
                line[pos] = 0; // null terminate
                if (pos > 0) {
                    ESP_LOGI(TAG, "Prompt: %s", line);

                    // Aquí llamas al LLM
                    char output[4096]; // buffer de salida (ajusta según necesidad)
                    int out_len = generate_with_output(transformer, tokenizer, sampler, line, output, sizeof(output));

                    // envía respuesta por UART
                    uart_write_bytes(UART_PORT_NUM, output, out_len);
                    uart_write_bytes(UART_PORT_NUM, "\n", 1);
                }
                pos = 0; // reset buffer
            } else {
                line[pos++] = byte;
            }
        }
    }
}

int generate_with_output(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler,
                         char *prompt, char *out_buf, size_t out_buf_len) {
    // Llamar a generate directamente, que imprime por UART
    generate(transformer, tokenizer, sampler, prompt, 256, NULL, NULL);

    // Para compilar, devuelvo un texto placeholder
    snprintf(out_buf, out_buf_len, "[LLM output]");
    return strlen(out_buf);
}
















// chatgpt v2
// Función de callback normal (no lambda)
void llm_response_callback(const char *resp) {
    uart_write_bytes(UART_PORT, resp, strlen(resp));
}

SemaphoreHandle_t llm_mutex;

void uart_llm_task_2(void *arg) {
    char line[UART_BUF_SIZE];
    int idx = 0;

    while (1) {
        uint8_t c;
        int read = uart_read_bytes(UART_PORT, &c, 1, portMAX_DELAY);
        if (read <= 0) continue;

        // ESP_LOGI(TAG, "Received char: %c (%d)", c, c); // Verbose debug

        if (c == 0x08 || c == 0x7F) { // Backspace or Delete
            if (idx > 0) {
                idx--;
                const char backspace_seq[] = "\b \b";
                uart_write_bytes(UART_PORT, backspace_seq, 3);
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            line[idx] = '\0';
            uart_write_bytes(UART_PORT, "\r\n", 2); // Echo newline properly
            
            if (strlen(line) == 0) {
                 idx = 0;
                 continue;
            }

            ESP_LOGI(TAG, "Processing prompt: '%s'", line);
            if (xSemaphoreTake(llm_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                ESP_LOGI(TAG, "Mutex taken, starting generation");
                // uart_write_bytes(UART_PORT, "\n", 1); 
                generate_with_callback(&transformer, &tokenizer, &sampler, line, llm_response_callback);
                ESP_LOGI(TAG, "Generation finished\r\n> ");
                xSemaphoreGive(llm_mutex);
            } else {
                ESP_LOGE(TAG, "Failed to take mutex, LLM busy");
                const char *msg = "LLM busy\r\n";
                uart_write_bytes(UART_PORT, msg, strlen(msg));
            }
            idx = 0; // Reset buffer AFTER processing
        } else {
            if (idx < UART_BUF_SIZE - 1) {
                line[idx++] = c;
                uart_write_bytes(UART_PORT, (char*)&c, 1); // Echo the character
            } else {
                ESP_LOGW(TAG, "UART buffer full, discarding char");
            }
        }
    }
}

void start_uart_llm_task() {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_PORT, &uart_config);
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    if (!llm_mutex) llm_mutex = xSemaphoreCreateMutex();

    xTaskCreate(uart_llm_task_2, "uart_llm", 8192, NULL, 5, NULL);
}

int generate_with_callback(Transformer *transformer, Tokenizer *tokenizer, Sampler *sampler,
                           const char *input, void (*callback)(const char *)) {
    generate(transformer, tokenizer, sampler, (char *)input, 256, NULL, callback);
    return 0;
}