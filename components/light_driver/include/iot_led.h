// Copyright 2017 Espressif Systems
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __IOT_LED_H__
#define __IOT_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/ledc.h"
#include "esp_err.h"
#include "driver/gpio.h"

#define GAMMA_CORRECTION   0.8f   /**< Gamma curve parameter */
#define GAMMA_TABLE_SIZE   256    /**< Gamma table size, used for led fade */
#define DUTY_SET_CYCLE     20     /**< Set duty cycle steps */

/**
 * @brief Initialize LED driver (LEDC timer)
 *
 * @param timer_num       LEDC timer index (LEDC_TIMER_0 ~ LEDC_TIMER_3)
 * @param speed_mode      LEDC speed mode (use LEDC_LOW_SPEED_MODE for ESP32-C3)
 * @param freq_hz         Frequency (Hz), typically < 5000 for LEDs
 * @param clk_cfg         Clock source (LEDC_USE_APB_CLK or LEDC_USE_REF_TICK)
 * @param duty_resolution Duty resolution (e.g. LEDC_TIMER_13_BIT)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG on wrong parameter
 */
esp_err_t iot_led_init(ledc_timer_t timer_num,
                       ledc_mode_t speed_mode,
                       uint32_t freq_hz,
                       ledc_clk_cfg_t clk_cfg,
                       ledc_timer_bit_t duty_resolution);

/**
 * @brief Deinitialize LED driver
 *
 * @return
 *      - ESP_OK on success
 */
esp_err_t iot_led_deinit(void);

/**
 * @brief Register one LED channel with GPIO
 *
 * @param channel   LEDC channel (LEDC_CHANNEL_0 ~ LEDC_CHANNEL_7 on ESP32-C3)
 * @param gpio_num  GPIO number
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if invalid channel or gpio
 */
esp_err_t iot_led_regist_channel(ledc_channel_t channel, gpio_num_t gpio_num);

/**
 * @brief Get current duty value of channel (0-255 mapped)
 *
 * @param channel   LEDC channel
 * @param dst       Pointer to store result
 * @return
 *      - ESP_OK on success
 */
esp_err_t iot_led_get_channel(ledc_channel_t channel, uint8_t *dst);

/**
 * @brief Set channel duty with fade
 *
 * @param channel   LEDC channel
 * @param value     Brightness (0–255)
 * @param fade_ms   Fade duration in ms (0 = immediate)
 * @return
 *      - ESP_OK on success
 */
esp_err_t iot_led_set_channel(ledc_channel_t channel, uint8_t value, uint32_t fade_ms);

/**
 * @brief Start blinking or loop fading
 *
 * @param channel    LEDC channel
 * @param value      Brightness (0–255)
 * @param period_ms  Blink period in ms
 * @param fade_flag  true = loop fade, false = simple blink
 * @return
 *      - ESP_OK on success
 */
esp_err_t iot_led_start_blink(ledc_channel_t channel, uint8_t value, uint32_t period_ms, bool fade_flag);

/**
 * @brief Stop blinking or loop fading
 *
 * @param channel   LEDC channel
 * @return
 *      - ESP_OK on success
 */
esp_err_t iot_led_stop_blink(ledc_channel_t channel);

/**
 * @brief Set gamma correction table (optional)
 *
 * @param gamma_table  Array size = GAMMA_TABLE_SIZE
 * @return
 *      - ESP_OK on success
 */
esp_err_t iot_led_set_gamma_table(const uint16_t gamma_table[GAMMA_TABLE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* __IOT_LED_H__ */
