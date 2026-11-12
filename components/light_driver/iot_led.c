// iot_led.c - ESP-IDF v5 compatible, fixed duty overflow
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "iot_led.h"
#include "math.h"

#define TAG "iot_led"
#define GAMMA_CORRECTION    (2.2f)

typedef struct {
    ledc_mode_t speed_mode;
    ledc_timer_t timer_num;
    ledc_timer_bit_t duty_resolution;
} iot_light_t;

static iot_light_t *g_light_config = NULL;
static uint16_t *g_gamma_table = NULL;

/* ---------------- Gamma Table ---------------- */
static void gamma_table_create(uint16_t *gamma_table, float correction, uint32_t max_duty) {
    for (int i = 0; i < GAMMA_TABLE_SIZE; i++) {
        float x = (float)i / (GAMMA_TABLE_SIZE - 1);
        float y = powf(x, 1.0f / correction);
        uint32_t duty = (uint32_t)(y * max_duty);
        if (duty > max_duty) duty = max_duty;
        gamma_table[i] = (uint16_t)duty;
    }
}

/* ---------------- Core Functions ---------------- */
esp_err_t iot_led_init(ledc_timer_t timer_num, ledc_mode_t speed_mode,
                       uint32_t freq_hz, ledc_clk_cfg_t clk_cfg, ledc_timer_bit_t duty_resolution)
{
    esp_err_t ret;

    const ledc_timer_config_t ledc_time_config = {
        .speed_mode      = speed_mode,
        .duty_resolution = duty_resolution,
        .timer_num       = timer_num,
        .freq_hz         = freq_hz,
        .clk_cfg         = clk_cfg,
    };

    ret = ledc_timer_config(&ledc_time_config);
    if (ret != ESP_OK) return ret;

    if (!g_light_config) {
        g_light_config = calloc(1, sizeof(iot_light_t));
        g_light_config->timer_num  = timer_num;
        g_light_config->speed_mode = speed_mode;
        g_light_config->duty_resolution = duty_resolution;
    }

    if (!g_gamma_table) {
        g_gamma_table = calloc(GAMMA_TABLE_SIZE, sizeof(uint16_t));
        uint32_t max_duty = (1 << duty_resolution) - 1;
        gamma_table_create(g_gamma_table, GAMMA_CORRECTION, max_duty);
    }

    return ESP_OK;
}

esp_err_t iot_led_deinit(void) {
    if (g_gamma_table) {
        free(g_gamma_table);
        g_gamma_table = NULL;
    }
    if (g_light_config) {
        free(g_light_config);
        g_light_config = NULL;
    }
    return ESP_OK;
}

esp_err_t iot_led_regist_channel(ledc_channel_t channel, gpio_num_t gpio_num) {
    if (!g_light_config) return ESP_ERR_INVALID_STATE;

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = gpio_num,
        .channel    = channel,
        .speed_mode = g_light_config->speed_mode,
        .timer_sel  = g_light_config->timer_num,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    return ledc_channel_config(&ch_cfg);
}

esp_err_t iot_led_set_channel(ledc_channel_t channel, uint8_t value, uint32_t fade_ms) {
    if (!g_light_config) return ESP_ERR_INVALID_STATE;

    uint32_t max_duty = (1 << g_light_config->duty_resolution) - 1;
    uint32_t duty = g_gamma_table[value];
    if (duty > max_duty) duty = max_duty;

    esp_err_t ret;
    if (fade_ms == 0) {
        ret = ledc_set_duty(g_light_config->speed_mode, channel, duty);
        if (ret == ESP_OK) ret = ledc_update_duty(g_light_config->speed_mode, channel);
    } else {
        ret = ledc_set_fade_with_time(g_light_config->speed_mode, channel, duty, fade_ms);
        if (ret == ESP_OK) ret = ledc_fade_start(g_light_config->speed_mode, channel, LEDC_FADE_NO_WAIT);
    }
    return ret;
}

esp_err_t iot_led_start_blink(ledc_channel_t channel, uint8_t value, uint32_t period_ms, bool fade_flag) {
    if (!g_light_config) return ESP_ERR_INVALID_STATE;

    uint32_t max_duty = (1 << g_light_config->duty_resolution) - 1;
    uint32_t duty = g_gamma_table[value];
    if (duty > max_duty) duty = max_duty;

    uint32_t half_period = period_ms / 2;

    esp_err_t ret = ledc_set_fade_with_time(g_light_config->speed_mode, channel, duty, fade_flag ? half_period : 0);
    if (ret != ESP_OK) return ret;
    ret = ledc_fade_start(g_light_config->speed_mode, channel, LEDC_FADE_NO_WAIT);
    if (ret != ESP_OK) return ret;

    ret = ledc_set_fade_with_time(g_light_config->speed_mode, channel, 0, fade_flag ? half_period : 0);
    if (ret != ESP_OK) return ret;

    return ledc_fade_start(g_light_config->speed_mode, channel, LEDC_FADE_NO_WAIT);
}

esp_err_t iot_led_stop_blink(ledc_channel_t channel) {
    if (!g_light_config) return ESP_ERR_INVALID_STATE;
    return ledc_stop(g_light_config->speed_mode, channel, 0);
}
