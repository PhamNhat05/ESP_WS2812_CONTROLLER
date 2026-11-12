#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h> 
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <math.h>

// [FIX 2] Thêm thư viện này để sửa lỗi rtc_cpu_freq
#include "soc/rtc.h" 


#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_utils.h>

#include "esp_insights.h"
#include "esp_diagnostics.h"
#include "esp_diagnostics_metrics.h"
#include "app_insights.h"

#include "app_wifi.h"
#include "app_storage.h"
#include "app_priv.h"
#include "app_effects.h"
#include "app_mdns.h" 

static const char *TAG = "app_main"; // Biến này sẽ được dùng để log
extern const char ota_server_cert[] asm("_binary_server_crt_start");

esp_rmaker_device_t *light_device;
SemaphoreHandle_t g_pm_update_sem = NULL;
extern bool g_is_resetting;

extern void local_control_init(void);

/* ================== CALLBACK TỪ APP/CLOUD ================== */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (g_is_resetting) return ESP_FAIL;

    const char *name = esp_rmaker_param_get_name(param);
    
    // GỌI SANG APP_EFFECTS.C ĐỂ XỬ LÝ LOGIC
    if (strcmp(name, ESP_RMAKER_DEF_POWER_NAME) == 0) {
        app_effects_set_power(val.val.b);
    } 
    else if (strcmp(name, ESP_RMAKER_DEF_BRIGHTNESS_NAME) == 0) {
        app_effects_set_brightness(val.val.i);
    } 
    else if (strcmp(name, "Hue Start") == 0) {
        app_effect_state_t s = app_effects_get_state();
        app_effects_set_hue(val.val.i, s.hue_end);
    }
    else if (strcmp(name, "Hue End") == 0) {
        app_effect_state_t s = app_effects_get_state();
        app_effects_set_hue(s.hue_start, val.val.i);
    }
    else if (strcmp(name, ESP_RMAKER_DEF_SATURATION_NAME) == 0) {
        app_effects_set_saturation(val.val.i);
    }
    else if (strcmp(name, "Mode") == 0) {
        int m = 0;
        if (strcmp(val.val.s, "Wave") == 0) m = 1;
        else if (strcmp(val.val.s, "Wave 2") == 0) m = 2;
        else if (strcmp(val.val.s, "Rainbow") == 0) m = 3;
        app_effects_set_mode(m);
    } 
    else if (strcmp(name, "Speed") == 0) {
        int s = 0;
        if (strcmp(val.val.s, "Slow") == 0) s = 1;
        else if (strcmp(val.val.s, "Medium") == 0) s = 2;
        else if (strcmp(val.val.s, "Fast") == 0) s = 3;
        app_effects_set_speed(s);
    }

    esp_rmaker_param_update_and_report(param, val);
    if (g_pm_update_sem) xSemaphoreGive(g_pm_update_sem);
    return ESP_OK;
}

/* ================== CALLBACK TỪ NÚT BẤM (LOCAL) ================== */
// Hàm này được gọi từ app_driver.c
void button_next_mode_trigger(void) {
    if (!light_device || g_is_resetting) return;

    // 1. CHUYỂN HIỆU ỨNG NGAY LẬP TỨC (OFFLINE)
    app_effects_next_mode();

    // 2. ĐỒNG BỘ LÊN APP (ONLINE)
    // [FIX 1] Thay thế logic cũ bằng hàm esp_rmaker_param_update_and_report
    // Hàm này tự động cập nhật giá trị nội bộ, và nếu có mạng thì gửi lên Cloud
    
    app_effect_state_t s = app_effects_get_state();

    // Cập nhật nút nguồn trên App
    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME),
        esp_rmaker_bool(s.power)
    );

    const char *mode_str = "Static";
    if (s.mode == 1) mode_str = "Wave";
    else if (s.mode == 2) mode_str = "Wave 2";
    else if (s.mode == 3) mode_str = "Rainbow";

    if (s.mode != 4) { // 4 is OFF logic
        // Cập nhật Dropdown Mode trên App
        esp_rmaker_param_update_and_report(
            esp_rmaker_device_get_param_by_name(light_device, "Mode"),
            esp_rmaker_str(mode_str)
        );
    }
    
    ESP_LOGI(TAG, "Button sync done: Power=%d, Mode=%s", s.power, mode_str);

    if (g_pm_update_sem) xSemaphoreGive(g_pm_update_sem);
}

/* ================== INSIGHTS TASK ================== */
void insights_metric_task(void *arg)
{
    rtc_cpu_freq_config_t cpu_conf; 
    
    // Đợi một chút để hệ thống ổn định khi mới khởi động
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        // [CẢI TIẾN] Thay portMAX_DELAY bằng 60000ms (60 giây)
        // Nghĩa là: Nếu có thay đổi -> Báo ngay. Nếu không có gì thay đổi -> Cứ 60s báo 1 lần (Heartbeat)
        if (xSemaphoreTake(g_pm_update_sem, pdMS_TO_TICKS(60000)) == pdTRUE || true) {
            
            // Lấy trạng thái từ bộ não trung tâm
            app_effect_state_t s = app_effects_get_state();

            // 1. REPORT LIGHT METRICS
            // Lưu ý: Key string ở đây phải khớp với lúc đăng ký (esp_diag_metrics_register)
            esp_diag_metrics_add_uint("brightness", s.brightness);
            esp_diag_metrics_add_uint("mode",       s.mode);
            esp_diag_metrics_add_uint("speed",      s.speed);
            esp_diag_metrics_add_uint("hue_start",  s.hue_start);
            esp_diag_metrics_add_uint("hue_end",    s.hue_end);

            // 2. REPORT SYSTEM METRICS
            bool is_locked = s.power; // Power on = Lock acquired
            esp_diag_metrics_add_bool("pm_lock", is_locked);

            rtc_clk_cpu_freq_get_config(&cpu_conf);
            esp_diag_metrics_add_uint("cpu_freq", cpu_conf.freq_mhz);

            ESP_LOGI(TAG, ">> INSIGHTS SENT: Mode=%d | Speed=%d | Bri=%d", s.mode, s.speed, s.brightness);
            
            // Xóa semaphore dư thừa nếu lỡ nó được give nhiều lần liên tục
            xSemaphoreTake(g_pm_update_sem, 0); 
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mdns_service_init();
    }
}

/* ================== APP MAIN ================== */
void app_main(void)
{
    g_pm_update_sem = xSemaphoreCreateBinary();
    
    /* 1. Init Storage & PM & Driver & Effects */
    app_storage_init();
    app_pm_init();
    app_driver_init(); 
    app_effects_init();
    
    // KHỞI ĐỘNG HIỆU ỨNG NGAY LẬP TỨC (Không cần chờ WiFi)
    app_effects_start_tasks();

    /* 2. Init WiFi & RainMaker */
    app_wifi_init();
    
    esp_rmaker_config_t cfg = { .enable_time_sync = true };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "ESP RainMaker Device", "WS2812 Controller");
    light_device = esp_rmaker_lightbulb_device_create("Light", NULL, DEFAULT_POWER);
    esp_rmaker_device_add_cb(light_device, write_cb, NULL);

    /* Add Params */
  
    esp_rmaker_device_add_param(light_device, esp_rmaker_brightness_param_create(ESP_RMAKER_DEF_BRIGHTNESS_NAME, DEFAULT_BRIGHTNESS));
    esp_rmaker_device_add_param(light_device, esp_rmaker_saturation_param_create(ESP_RMAKER_DEF_SATURATION_NAME, DEFAULT_SATURATION));

    static const char *mode_vals[] = { "Static", "Wave", "Wave 2", "Rainbow" };
    esp_rmaker_param_t *mode = esp_rmaker_param_create("Mode", "esp.param.mode", esp_rmaker_str("Static"), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(mode, "esp.ui.dropdown");
    esp_rmaker_param_add_valid_str_list(mode, mode_vals, 4); 
    esp_rmaker_device_add_param(light_device, mode);

    esp_rmaker_param_t *hue_start = esp_rmaker_param_create("Hue Start", "app.param.hue_start", esp_rmaker_int(0), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(hue_start, "esp.ui.hue-slider");
    esp_rmaker_param_add_bounds(hue_start, esp_rmaker_int(0), esp_rmaker_int(360), esp_rmaker_int(1));
    esp_rmaker_device_add_param(light_device, hue_start);

    esp_rmaker_param_t *hue_end = esp_rmaker_param_create("Hue End", "app.param.hue_end", esp_rmaker_int(240), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(hue_end, "esp.ui.hue-slider");
    esp_rmaker_param_add_bounds(hue_end, esp_rmaker_int(0), esp_rmaker_int(360), esp_rmaker_int(1));
    esp_rmaker_device_add_param(light_device, hue_end);
    
    static const char *speed_vals[] = { "Stop", "Slow", "Medium", "Fast" };
    esp_rmaker_param_t *speed = esp_rmaker_param_create("Speed", "app.param.speed_select", esp_rmaker_str("Stop"), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(speed, "esp.ui.dropdown");
    esp_rmaker_param_add_valid_str_list(speed, speed_vals, 4);
    esp_rmaker_device_add_param(light_device, speed);

    esp_rmaker_node_add_device(node, light_device);
    
    /* 3. Start Services */
    app_insights_enable();
    
    // Đăng ký metrics
    // Nhóm "light"
    esp_diag_metrics_register("light", "brightness", "Brightness", "light.brightness", ESP_DIAG_DATA_TYPE_UINT);
    esp_diag_metrics_register("light", "mode",       "Mode",       "light.mode",       ESP_DIAG_DATA_TYPE_UINT);
    esp_diag_metrics_register("light", "speed",      "Speed",      "light.speed",      ESP_DIAG_DATA_TYPE_UINT);
    esp_diag_metrics_register("light", "hue_start",  "Hue Start",  "light.hue_start",  ESP_DIAG_DATA_TYPE_UINT);
    esp_diag_metrics_register("light", "hue_end",    "Hue End",    "light.hue_end",    ESP_DIAG_DATA_TYPE_UINT);
    
    // Nhóm "system"
    esp_diag_metrics_register("system", "pm_lock",   "PM Lock",    "sys.pm_lock",      ESP_DIAG_DATA_TYPE_BOOL);
    esp_diag_metrics_register("system", "cpu_freq",  "CPU Freq",   "sys.cpu_freq",     ESP_DIAG_DATA_TYPE_UINT);

    esp_rmaker_ota_config_t ota_cfg = { .server_cert = ota_server_cert };
    esp_rmaker_ota_enable(&ota_cfg, OTA_USING_TOPICS);

    esp_rmaker_start();
    
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
    app_wifi_start(POP_TYPE_FIXED);
    local_control_init();

    xTaskCreate(insights_metric_task, "insights", 4096, NULL, 5, NULL);
    if (g_pm_update_sem) xSemaphoreGive(g_pm_update_sem);
}