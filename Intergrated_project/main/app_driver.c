
/* app_driver.c */
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "iot_button.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "driver/gpio.h"
#include "esp_rmaker_core.h"
#include "esp_rom_gpio.h"
#include "app_priv.h"
#include <esp_rmaker_core.h>
#include <esp_rmaker_utils.h> 

#define TAG "app_driver"

/* Config GPIO */
#define WS2812_GPIO 0
#define WS2812_LED_NUM 8
#define LIGHT_BUTTON_GPIO 3
#define RESET_BTN_GPIO 10
#define REBOOT_DELAY 2

/* External Call to Main */
extern void button_next_mode_trigger(void);

/* Globals */
static led_strip_handle_t strip = NULL;
bool g_is_resetting = false;
static SemaphoreHandle_t g_led_mutex = NULL; // Bảo vệ LED strip

/* ================== HELPER: SAFE REFRESH ================== */
static void safe_led_refresh() {
    if (!strip || g_is_resetting) return;
    if (g_led_mutex) {
        if (xSemaphoreTake(g_led_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            led_strip_refresh(strip);
            xSemaphoreGive(g_led_mutex);
        }
    }
}

/* ================== PUBLIC PIXEL SET ================== */
// Hàm này được app_effects.c gọi
void app_driver_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (strip && index >= 0 && index < WS2812_LED_NUM) {
        // Pixel buffer set rất nhanh, không cần lock mutex từng pixel để tối ưu
        led_strip_set_pixel(strip, index, r, g, b);
    }
}

void app_driver_show(void) {
    safe_led_refresh();
}

/* ================== BUTTON CALLBACK ================== */
static void push_btn_cb(void *arg) {
    if (g_is_resetting) return;
    ESP_LOGI(TAG, "Button Pressed -> Triggering Next Mode");
    
    // Gọi sang app_main để xử lý logic (đổi hiệu ứng + báo cloud)
    button_next_mode_trigger(); 
}

/* ================== RESET LOGIC ================== */
static void reset_led_blink_forever(void *arg) {
    while (1) {
        if(g_led_mutex) xSemaphoreTake(g_led_mutex, portMAX_DELAY);
        for (int i = 0; i < WS2812_LED_NUM; i++) led_strip_set_pixel(strip, i, 255, 0, 0);
        led_strip_refresh(strip);
        if(g_led_mutex) xSemaphoreGive(g_led_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));

        if(g_led_mutex) xSemaphoreTake(g_led_mutex, portMAX_DELAY);
        led_strip_clear(strip);
        led_strip_refresh(strip);
        if(g_led_mutex) xSemaphoreGive(g_led_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void reset_btn_task(void *arg) {
    esp_rom_gpio_pad_select_gpio(RESET_BTN_GPIO);
    gpio_set_direction(RESET_BTN_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(RESET_BTN_GPIO);
    bool was_pressed = false;

    while (1) {
        if (g_is_resetting) { vTaskDelay(1000/portTICK_PERIOD_MS); continue; }
        bool pressed = (gpio_get_level(RESET_BTN_GPIO) == 0);
        
        if (pressed && !was_pressed) {
            vTaskDelay(pdMS_TO_TICKS(30)); // debounce
            if (gpio_get_level(RESET_BTN_GPIO) != 0) continue;
            
            was_pressed = true;
            g_is_resetting = true;
            ESP_LOGW(TAG, "FACTORY RESET TRIGGERED");
            
            if(g_led_mutex) xSemaphoreTake(g_led_mutex, portMAX_DELAY);
            led_strip_clear(strip);
            led_strip_refresh(strip);
            if(g_led_mutex) xSemaphoreGive(g_led_mutex);
            
            xTaskCreate(reset_led_blink_forever, "rst_blink", 2048, NULL, 5, NULL);
            esp_rmaker_factory_reset(0, REBOOT_DELAY);
        }
        if (!pressed) was_pressed = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ================== INIT ================== */
void app_driver_init(void) {
    g_led_mutex = xSemaphoreCreateMutex();

    /* Init Button */
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = LIGHT_BUTTON_GPIO,
            .active_level = 0
        }
    };
    button_handle_t btn = iot_button_create(&btn_cfg);
    if (btn) iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, push_btn_cb);

    /* Init RMT LED Strip */
    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = WS2812_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));

    xTaskCreate(reset_btn_task, "reset_btn", 2048, NULL, 5, NULL);
}

// PM Lock Stubs (để tương thích)
esp_err_t app_light_set_power(bool power) { return ESP_OK; }