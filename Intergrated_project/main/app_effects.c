/* app_effects.c */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_pm.h"

#include "app_effects.h"

// Gọi các hàm ngoại vi từ app_driver.c
extern void app_driver_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b);
extern void app_driver_show(void);
extern esp_err_t app_pm_lock_acquire(void);
extern esp_err_t app_pm_lock_release(void);
extern SemaphoreHandle_t g_pm_update_sem;

static const char *TAG = "app_effects";

// Biến toàn cục nội bộ (Local State)
static app_effect_state_t g_state = {
    .power = true,
    .mode = EFFECT_MODE_STATIC,
    .speed = 3, // Fast
    .hue_start = 0,
    .hue_end = 240,
    .brightness = 80,
    .saturation = 100
};

// Biến hỗ trợ hiệu ứng Breath (Dimming)
static int dim_dir = 1;
static uint16_t dim_curr = 50;

// --- HÀM PHỤ TRỢ: HSV -> RGB ---
static void hsv2rgb_local(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float hf = h / 60.0f;
    int i = (int)hf;
    float f = hf - i;
    float pv = v * (1.0f - s / 100.0f);
    float qv = v * (1.0f - s / 100.0f * f);
    float tv = v * (1.0f - s / 100.0f * (1.0f - f));

    uint8_t V = (v * 255) / 100;
    uint8_t P = (pv * 255) / 100;
    uint8_t Q = (qv * 255) / 100;
    uint8_t T = (tv * 255) / 100;

    switch (i % 6) {
        case 0: *r = V; *g = T; *b = P; break;
        case 1: *r = Q; *g = V; *b = P; break;
        case 2: *r = P; *g = V; *b = T; break;
        case 3: *r = P; *g = Q; *b = V; break;
        case 4: *r = T; *g = P; *b = V; break;
        case 5: *r = V; *g = P; *b = Q; break;
    }
}

// --- UPDATE STATIC COLOR ---
static void update_static_color() {
    if (!g_state.power) {
        for (int i = 0; i < LED_COUNT; i++) app_driver_set_pixel(i, 0, 0, 0);
        app_driver_show();
        return;
    }
    uint8_t r, g, b;
    hsv2rgb_local(g_state.hue_start, g_state.saturation, g_state.brightness, &r, &g, &b);
    for (int i = 0; i < LED_COUNT; i++) app_driver_set_pixel(i, r, g, b);
    app_driver_show();
}

// ================== TASKS HIỆU ỨNG ==================

/* 1. Static Breath Task */
static void dimming_task(void *arg) {
    while (1) {
        if (!g_state.power || g_state.mode != EFFECT_MODE_STATIC || g_state.speed == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int period_ms = (g_state.speed == 3) ? 1000 : (g_state.speed == 2 ? 2000 : 3000);
        const int step_delay = 35;
        int steps = (period_ms / 2) / step_delay;
        if (steps < 1) steps = 1;
        int step_val = 100 / steps;
        if (step_val < 1) step_val = 1;

        if (dim_dir > 0) {
            dim_curr += step_val;
            if (dim_curr >= 100) {
                dim_curr = 100; dim_dir = -1;
                // Pause at peak
                for(int k=0; k<20; k++) { 
                     if(g_state.mode != EFFECT_MODE_STATIC || g_state.speed == 0) break;
                     vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        } else {
            if (dim_curr <= step_val) {
                dim_curr = 0; dim_dir = 1;
                // Pause at bottom
                for(int k=0; k<20; k++) { 
                     if(g_state.mode != EFFECT_MODE_STATIC || g_state.speed == 0) break;
                     vTaskDelay(pdMS_TO_TICKS(50));
                }
            } else {
                dim_curr -= step_val;
            }
        }
        uint8_t r, g, b;
        hsv2rgb_local(g_state.hue_start, g_state.saturation, dim_curr, &r, &g, &b);
        for (int i=0; i<LED_COUNT; i++) app_driver_set_pixel(i, r, g, b);
        app_driver_show();
        vTaskDelay(pdMS_TO_TICKS(step_delay));
    }
}

/* 2. Wave Task */
static void wave_task(void *arg) {
    int head_pos = 0;
    while (1) {
        if (!g_state.power || g_state.mode != EFFECT_MODE_WAVE || g_state.speed == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int delay_ms = (g_state.speed == 3) ? 50 : (g_state.speed == 2 ? 100 : 200);

        for (int i = 0; i < LED_COUNT; i++) {
            int distance = (head_pos - i + LED_COUNT) % LED_COUNT;
            if (distance < TRAIN_LEN) {
                int hue_diff = (int)g_state.hue_end - (int)g_state.hue_start;
                int current_hue = g_state.hue_start + (hue_diff * distance / (TRAIN_LEN - 1));
                while (current_hue < 0) current_hue += 360;
                current_hue %= 360;
                uint8_t r, g, b;
                hsv2rgb_local((uint16_t)current_hue, 100, g_state.brightness, &r, &g, &b);
                app_driver_set_pixel(i, r, g, b);
            } else {
                app_driver_set_pixel(i, 0, 0, 0);
            }
        }
        app_driver_show();
        head_pos++;
        if (head_pos >= LED_COUNT) head_pos = 0;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* 3. Wave 2 Task */
static void wave2_task(void *arg) {
    int head_pos = 0;
    int dir = 1;
    while (1) {
        if (!g_state.power || g_state.mode != EFFECT_MODE_WAVE2 || g_state.speed == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int delay_ms = (g_state.speed == 3) ? 50 : (g_state.speed == 2 ? 100 : 200);

        for (int i = 0; i < LED_COUNT; i++) {
            int distance = (dir > 0) ? (head_pos - i) : (i - head_pos);
            if (distance >= 0 && distance < TRAIN_LEN) {
                int hue_diff = (int)g_state.hue_end - (int)g_state.hue_start;
                int current_hue = g_state.hue_start + (hue_diff * distance / (TRAIN_LEN - 1));
                while (current_hue < 0) current_hue += 360;
                current_hue %= 360;
                uint8_t r, g, b;
                hsv2rgb_local((uint16_t)current_hue, 100, g_state.brightness, &r, &g, &b);
                app_driver_set_pixel(i, r, g, b);
            } else {
                app_driver_set_pixel(i, 0, 0, 0);
            }
        }
        app_driver_show();
        head_pos += dir;
        if (dir > 0 && head_pos >= LED_COUNT + TRAIN_LEN - 1) { dir = -1; head_pos = LED_COUNT - 1; }
        else if (dir < 0 && head_pos <= -TRAIN_LEN) { dir = 1; head_pos = 0; }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* 4. Rainbow Task */
static void rainbow_task(void *arg) {
    int hue = 0;
    while (1) {
        if (!g_state.power || g_state.mode != EFFECT_MODE_RAINBOW || g_state.speed == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int delay_ms = (g_state.speed == 3) ? 20 : (g_state.speed == 2 ? 50 : 100);
        uint8_t r, g, b;
        hsv2rgb_local(hue, 100, g_state.brightness, &r, &g, &b);
        for(int i=0; i<LED_COUNT; i++) app_driver_set_pixel(i, r, g, b);
        app_driver_show();
        hue = (hue + 5) % 360;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// ================== PUBLIC API ==================

void app_effects_init(void) {
    ESP_LOGI(TAG, "Effects Initialized");
    if (g_state.power) app_pm_lock_acquire();
}

void app_effects_start_tasks(void) {
    xTaskCreate(dimming_task, "dimming", 4096, NULL, 6, NULL);
    xTaskCreate(wave_task,    "wave",    4096, NULL, 6, NULL);
    xTaskCreate(wave2_task,   "wave2",   4096, NULL, 6, NULL);
    xTaskCreate(rainbow_task, "rainbow", 4096, NULL, 6, NULL);
}

app_effect_state_t app_effects_get_state(void) {
    return g_state;
}

// Hàm này được gọi khi nhấn nút cứng -> Cập nhật Local ngay lập tức
void app_effects_next_mode(void) {
    // 1. Clear LED cũ
    for (int i = 0; i < LED_COUNT; i++) app_driver_set_pixel(i, 0, 0, 0);
    app_driver_show();

    // 2. Tăng Mode
    g_state.mode++;
    if (g_state.mode > EFFECT_MODE_OFF) { 
        g_state.mode = 0; // Quay về Static
    }

    // 3. Preset cấu hình cứng
    if (g_state.mode != EFFECT_MODE_OFF) {
        g_state.power = true;
        app_pm_lock_acquire();
        if (g_state.brightness < 50) g_state.brightness = 100;
    }

    switch (g_state.mode) {
        case EFFECT_MODE_STATIC: // Static Warm
            g_state.speed = 0;
            g_state.hue_start = 30; g_state.hue_end = 30;
            update_static_color();
            break;
        case EFFECT_MODE_WAVE: // Wave Red-Blue
            g_state.speed = 3;
            g_state.hue_start = 0; g_state.hue_end = 240;
            break;
        case EFFECT_MODE_WAVE2: // Wave2 Green-Purple
            g_state.speed = 3;
            g_state.hue_start = 120; g_state.hue_end = 280;
            break;
        case EFFECT_MODE_RAINBOW:
            g_state.speed = 3;
            break;
        case EFFECT_MODE_OFF:
            g_state.power = false;
            g_state.speed = 0;
            app_pm_lock_release();
            break;
    }
    ESP_LOGI(TAG, "NEXT MODE: %d", g_state.mode);
    if (g_pm_update_sem) xSemaphoreGive(g_pm_update_sem);
}

// --- SETTERS TỪ APP ---
void app_effects_set_power(bool power) {
    g_state.power = power;
    if (power) {
        app_pm_lock_acquire();
        if (g_state.mode == EFFECT_MODE_STATIC) update_static_color();
    } else {
        app_pm_lock_release();
        for (int i=0; i<LED_COUNT; i++) app_driver_set_pixel(i,0,0,0);
        app_driver_show();
    }
}

void app_effects_set_mode(int mode) {
    g_state.mode = mode;
    // Nếu App chọn Static -> Cập nhật màu ngay
    if (mode == EFFECT_MODE_STATIC) update_static_color();
    if (g_pm_update_sem) xSemaphoreGive(g_pm_update_sem);
}

void app_effects_set_speed(int speed) {
    g_state.speed = speed;
    if (g_state.mode == EFFECT_MODE_STATIC && speed == 0) {
        dim_curr = g_state.brightness;
        update_static_color();
    }
}

void app_effects_set_brightness(uint16_t val) {
    g_state.brightness = val;
    dim_curr = val;
    if (g_state.mode == EFFECT_MODE_STATIC) update_static_color();
}

void app_effects_set_hue(uint16_t start, uint16_t end) {
    g_state.hue_start = start;
    g_state.hue_end = end;
    if (g_state.mode == EFFECT_MODE_STATIC) update_static_color();
}

void app_effects_set_saturation(uint16_t sat) {
    g_state.saturation = sat;
    if (g_state.mode == EFFECT_MODE_STATIC) update_static_color();
}