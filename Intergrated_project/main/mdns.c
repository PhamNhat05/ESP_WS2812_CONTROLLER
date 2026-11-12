/* mdns.c */
#include <string.h>
#include "esp_log.h"
#include "mdns.h"       // Thư viện hệ thống
#include "esp_netif.h"
#include "app_mdns.h"   // [THÊM] Header của chúng ta

static const char *TAG = "mDNS";

/* =====================================================
 * INIT MDNS
 * ===================================================== */
void mdns_service_init(void)
{
    esp_err_t err;

    err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init failed: %d", err);
        return;
    }

    /* Hostname: http://ESP-Night-Fury.local */
    mdns_hostname_set("ESP-Night-Fury");
    mdns_instance_name_set("Night Fury Local Control");

    /* Quảng bá HTTP service */
    mdns_service_add(
        "NightFury-Web",   // instance
        "_http",           // service
        "_tcp",            // proto
        80,                // port
        NULL,              // txt
        0
    );

    ESP_LOGI(TAG, "mDNS started: http://ESP-Night-Fury.local");
}