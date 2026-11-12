// Copyright 2020 Espressif Systems (Shanghai) Co. Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "button_adc.h"
#include "esp_timer.h"

static const char *TAG = "adc button";

#define ADC_BTN_CHECK(a, str, ret_val)                          \
    if (!(a))                                                     \
    {                                                             \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret_val);                                         \
    }

#define DEFAULT_VREF    1100
#define NO_OF_SAMPLES   CONFIG_ADC_BUTTON_SAMPLE_TIMES     //Multisampling

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32C3
#define ADC_BUTTON_WIDTH       ADC_BITWIDTH_12
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_BUTTON_WIDTH       ADC_BITWIDTH_13
#endif
#define ADC_BUTTON_ATTEN       ADC_ATTEN_DB_12
#define ADC_BUTTON_ADC_UNIT    ADC_UNIT_1
#define ADC_BUTTON_MAX_CHANNEL CONFIG_ADC_BUTTON_MAX_CHANNEL
#define ADC_BUTTON_MAX_BUTTON  CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL

typedef struct {
    uint16_t min;
    uint16_t max;
} button_data_t;

typedef struct {
    adc_channel_t channel;
    uint8_t is_init;
    button_data_t btns[ADC_BUTTON_MAX_BUTTON];  /* all button on the channel */
    uint64_t last_time;  /* the last time of adc sample */
} btn_adc_channel_t;

typedef struct {
    bool is_configured;
    adc_cali_handle_t adc_chars;   // đổi esp_adc_cal_characteristics_t -> adc_cali_handle_t
    adc_oneshot_unit_handle_t adc_handle; // thêm handle cho oneshot
    btn_adc_channel_t ch[ADC_BUTTON_MAX_CHANNEL];
    uint8_t ch_num;
} adc_button_t;

static adc_button_t g_button = {0};

static int find_unused_channel(void)
{
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (0 == g_button.ch[i].is_init) {
            return i;
        }
    }
    return -1;
}

static int find_channel(adc_channel_t channel)
{
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (channel == g_button.ch[i].channel) {
            return i;
        }
    }
    return -1;
}

esp_err_t button_adc_init(const button_adc_config_t *config)
{
    ADC_BTN_CHECK(NULL != config, "Pointer of config is invalid", ESP_ERR_INVALID_ARG);
    ADC_BTN_CHECK(config->adc_channel < ADC_CHANNEL_1, "channel out of range", ESP_ERR_NOT_SUPPORTED);
    ADC_BTN_CHECK(config->button_index < ADC_BUTTON_MAX_BUTTON, "button_index out of range", ESP_ERR_NOT_SUPPORTED);
    ADC_BTN_CHECK(config->max > 0, "key max voltage invalid", ESP_ERR_INVALID_ARG);

    int ch_index = find_unused_channel();
    if (ch_index >= 0) { /**< the channel has been initialized */
        ADC_BTN_CHECK(g_button.ch[ch_index].btns[config->button_index].max == 0, "The button_index has been used", ESP_ERR_INVALID_STATE);
    } else { /**< this is a new channel */
        int unused_ch_index = find_unused_channel();
        ADC_BTN_CHECK(unused_ch_index >= 0, "exceed max channel number, can't create a new channel", ESP_ERR_INVALID_STATE);
        ch_index = unused_ch_index;
    }

    /** initialize adc */
    if (0 == g_button.is_configured) {
        //Configure ADC oneshot unit
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_BUTTON_ADC_UNIT,
        };
        adc_oneshot_new_unit(&init_config, &g_button.adc_handle);

        //Characterize ADC (new calibration API)
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_BUTTON_ADC_UNIT,
            .atten = ADC_BUTTON_ATTEN,
            .bitwidth = ADC_BUTTON_WIDTH,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_config, &g_button.adc_chars) == ESP_OK) {
            ESP_LOGI(TAG, "ADC Calibration init OK (Line Fitting)");
        } else {
            ESP_LOGW(TAG, "ADC Calibration not supported, continue without calibration");
            g_button.adc_chars = NULL;
        }

        g_button.is_configured = 1;
    }

    /** initialize adc channel */
    if (0 == g_button.ch[ch_index].is_init) {
        adc_oneshot_chan_cfg_t chan_config = {
            .bitwidth = ADC_BUTTON_WIDTH,
            .atten = ADC_BUTTON_ATTEN,
        };
        adc_oneshot_config_channel(g_button.adc_handle, config->adc_channel, &chan_config);

        g_button.ch[ch_index].channel = config->adc_channel;
        g_button.ch[ch_index].is_init = 1;
        g_button.ch[ch_index].last_time = 0;
    }
    g_button.ch[ch_index].btns[config->button_index].max = config->max;
    g_button.ch[ch_index].btns[config->button_index].min = config->min;
    g_button.ch_num++;

    return ESP_OK;
}

esp_err_t button_adc_deinit(adc_channel_t channel, int button_index)
{
    ADC_BTN_CHECK(channel < ADC_CHANNEL_1, "channel out of range", ESP_ERR_INVALID_ARG);
    ADC_BTN_CHECK(button_index < ADC_BUTTON_MAX_BUTTON, "button_index out of range", ESP_ERR_INVALID_ARG);

    int ch_index = find_channel(channel);
    ADC_BTN_CHECK(ch_index >= 0, "can't find the channel", ESP_ERR_INVALID_ARG);

    g_button.ch[ch_index].btns[button_index].max = 0;
    g_button.ch[ch_index].btns[button_index].min = 0;

    /** check button usage on the channel*/
    uint8_t unused_button = 0;
    for (size_t i = 0; i < ADC_BUTTON_MAX_BUTTON; i++) {
        if (0 == g_button.ch[ch_index].btns[i].max) {
            unused_button++;
        }
    }
    if (unused_button == ADC_BUTTON_MAX_BUTTON && g_button.ch[ch_index].is_init) {  /**< if all button is unused, deinit the channel */
        g_button.ch[ch_index].is_init = 0;
        g_button.ch[ch_index].channel = ADC_CHANNEL_0;
        ESP_LOGD(TAG, "all button is unused on channel%d, deinit the channel", g_button.ch[ch_index].channel);
    }

    /** check channel usage on the adc*/
    uint8_t unused_ch = 0;
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {
        if (0 == g_button.ch[i].is_init) {
            unused_ch++;
        }
    }
    if (unused_ch == ADC_BUTTON_MAX_CHANNEL && g_button.is_configured) { /**< if all channel is unused, deinit the adc */
        if (g_button.adc_chars) {
            adc_cali_delete_scheme_curve_fitting(g_button.adc_chars);
        }
        adc_oneshot_del_unit(g_button.adc_handle);
        memset(&g_button, 0, sizeof(adc_button_t));
        ESP_LOGD(TAG, "all channel is unused, , deinit adc");
    }

    return ESP_OK;
}

static uint32_t get_adc_volatge(adc_channel_t channel)
{
    int adc_raw = 0;
    int adc_reading = 0;

    //Multisampling
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        adc_oneshot_read(g_button.adc_handle, channel, &adc_raw);
        adc_reading += adc_raw;
    }
    adc_reading /= NO_OF_SAMPLES;

    //Convert adc_reading to voltage in mV
    int voltage = 0;
    if (g_button.adc_chars) {
        adc_cali_raw_to_voltage(g_button.adc_chars, adc_reading, &voltage);
    }
    ESP_LOGV(TAG, "Raw: %d\tVoltage: %dmV", adc_reading, voltage);
    return voltage;
}

uint8_t button_adc_get_key_level(void *button_index)
{
    static uint16_t vol = 0;
    uint32_t ch = ADC_BUTTON_SPLIT_CHANNEL(button_index);
    uint32_t index = ADC_BUTTON_SPLIT_INDEX(button_index);
    ADC_BTN_CHECK(ch < ADC_CHANNEL_1, "channel out of range", 0);
    ADC_BTN_CHECK(index < ADC_BUTTON_MAX_BUTTON, "button_index out of range", 0);
    int ch_index = find_channel(ch);
    ADC_BTN_CHECK(ch_index >= 0, "The button_index is not init", 0);

    /** It starts only when the elapsed time is more than 1ms */
    if ((esp_timer_get_time() - g_button.ch[ch_index].last_time) > 1000) {
        vol = get_adc_volatge(ch);
        g_button.ch[ch_index].last_time = esp_timer_get_time();
    }

    if (vol <= g_button.ch[ch_index].btns[index].max &&
            vol > g_button.ch[ch_index].btns[index].min) {
        return 1;
    }
    return 0;
}
