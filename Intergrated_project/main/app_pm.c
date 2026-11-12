#include <stdio.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_pm.h"
#include "app_priv.h"
#include "sdkconfig.h"

static const char *TAG = "app_pm";

/* Cấu hình tần số CPU */
#define DEFAULT_MAX_FREQ_MHZ    80
#define DEFAULT_MIN_FREQ_MHZ    10

/* Biến quản lý Lock */
static esp_pm_lock_handle_t g_pm_apb_lock = NULL;
static bool g_is_locked = false;

/* * Khởi tạo quản lý năng lượng */
esp_err_t app_pm_init(void)
{
#if CONFIG_PM_ENABLE
    /* 1. Cấu hình Dynamic Frequency Scaling (DFS) */
    esp_pm_config_t pm_config = {
        .max_freq_mhz = DEFAULT_MAX_FREQ_MHZ,
        .min_freq_mhz = DEFAULT_MIN_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true // Cho phép ngủ nhẹ khi hệ thống rảnh
#endif
    };

    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PM setup failed. Check menuconfig > Power Management");
        return err;
    } else {
        ESP_LOGI(TAG, "PM setup OK. Max: %dMHz, Min: %dMHz", 
                 DEFAULT_MAX_FREQ_MHZ, DEFAULT_MIN_FREQ_MHZ);
    }

    /* 2. Tạo Lock APB */
    err = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "ws2812_lock", &g_pm_apb_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PM lock");
        return err;
    }
#else
    ESP_LOGI(TAG, "Power Management is disabled in menuconfig");
#endif
    return ESP_OK;
}

/* * Gọi hàm này khi đèn BẬT */
esp_err_t app_pm_lock_acquire(void)
{
#if CONFIG_PM_ENABLE
    if (g_pm_apb_lock && !g_is_locked) {
        esp_pm_lock_acquire(g_pm_apb_lock);
        g_is_locked = true;
        // ESP_LOGI(TAG, "Power Lock ACQUIRED (High Performance)");
    }
#endif
    return ESP_OK;
}

/* * Gọi hàm này khi đèn TẮT */
esp_err_t app_pm_lock_release(void)
{
#if CONFIG_PM_ENABLE
    if (g_pm_apb_lock && g_is_locked) {
        esp_pm_lock_release(g_pm_apb_lock);
        g_is_locked = false;
        // ESP_LOGI(TAG, "Power Lock RELEASED (Power Saving)");
    }
#endif
    return ESP_OK;
}

/* ============================================================
 * [FIX] ĐÂY LÀ HÀM BẠN ĐANG THIẾU
 * Hàm này giúp app_main biết được CPU đang chạy nhanh hay chậm
 * ============================================================ */
bool app_pm_is_locked(void)
{
    return g_is_locked;
}