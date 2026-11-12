/* localcontrol.c */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_params.h"
#include "cJSON.h" // Sử dụng cJSON có sẵn trong IDF cho an toàn

#include "app_effects.h" // [QUAN TRỌNG] Gọi thư viện hiệu ứng

static const char *TAG = "local_control";

/* RainMaker Device Handle từ app_main.c */
extern esp_rmaker_device_t *light_device;

/* =====================================================
 * API: GET STATE (Lấy trạng thái trả về Web)
 * ===================================================== */
static esp_err_t api_get_state(httpd_req_t *req)
{
    // 1. Lấy trạng thái thực tế từ bộ não app_effects
    app_effect_state_t s = app_effects_get_state();

    // 2. Tạo JSON thủ công (cho nhanh và nhẹ như code cũ của bạn)
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{"
        "\"power\":%s,"
        "\"brightness\":%d,"
        "\"saturation\":%d,"
        "\"hue_start\":%d,"
        "\"hue_end\":%d,"
        "\"mode\":%d,"
        "\"speed\":%d"
        "}",
        s.power ? "true" : "false",
        s.brightness, s.saturation,
        s.hue_start, s.hue_end,
        s.mode, s.speed
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* =====================================================
 * API: SET STATE (Web gửi lệnh xuống)
 * ===================================================== */
static esp_err_t api_set_state(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    /* Dùng cJSON parse cho an toàn và chuẩn xác hơn strstr */
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    bool need_report = false;

    // 1. XỬ LÝ POWER
    cJSON *item = cJSON_GetObjectItem(root, "power");
    if (item) {
        bool p = cJSON_IsTrue(item);
        app_effects_set_power(p); // Điều khiển LED ngay lập tức
        
        // Cập nhật RainMaker
        if(light_device) {
             esp_rmaker_param_update_and_report(
                esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME),
                esp_rmaker_bool(p));
        }
    }

    // 2. XỬ LÝ BRIGHTNESS
    item = cJSON_GetObjectItem(root, "brightness");
    if (item) {
        app_effects_set_brightness(item->valueint);
        if(light_device) {
            esp_rmaker_param_update_and_report(
                esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_BRIGHTNESS_NAME),
                esp_rmaker_int(item->valueint));
        }
    }

    // 3. XỬ LÝ SATURATION
    item = cJSON_GetObjectItem(root, "saturation");
    if (item) {
        app_effects_set_saturation(item->valueint);
        if(light_device) {
            esp_rmaker_param_update_and_report(
                esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_SATURATION_NAME),
                esp_rmaker_int(item->valueint));
        }
    }

    // 4. XỬ LÝ HUE START/END
    cJSON *hs = cJSON_GetObjectItem(root, "hue_start");
    cJSON *he = cJSON_GetObjectItem(root, "hue_end");
    if (hs || he) {
        app_effect_state_t cur = app_effects_get_state();
        int new_s = hs ? hs->valueint : cur.hue_start;
        int new_e = he ? he->valueint : cur.hue_end;
        app_effects_set_hue(new_s, new_e);
        
        if(light_device && hs) esp_rmaker_param_update_and_report(esp_rmaker_device_get_param_by_name(light_device, "Hue Start"), esp_rmaker_int(new_s));
        if(light_device && he) esp_rmaker_param_update_and_report(esp_rmaker_device_get_param_by_name(light_device, "Hue End"), esp_rmaker_int(new_e));
    }

    // 5. XỬ LÝ SPEED
    item = cJSON_GetObjectItem(root, "speed");
    if (item) {
        app_effects_set_speed(item->valueint);
        
        // Map lại String cho RainMaker
        const char *sp_str = "Stop";
        if(item->valueint == 1) sp_str = "Slow";
        if(item->valueint == 2) sp_str = "Medium";
        if(item->valueint == 3) sp_str = "Fast";

        if(light_device) {
            esp_rmaker_param_update_and_report(
                esp_rmaker_device_get_param_by_name(light_device, "Speed"),
                esp_rmaker_str(sp_str));
        }
    }

    // 6. XỬ LÝ MODE
    item = cJSON_GetObjectItem(root, "mode");
    if (item) {
        int m = item->valueint;
        app_effects_set_mode(m);

        // Map lại String cho RainMaker
        const char *m_str = "Static";
        if(m == 1) m_str = "Wave";
        if(m == 2) m_str = "Wave 2";
        if(m == 3) m_str = "Rainbow";

        if(light_device) {
            esp_rmaker_param_update_and_report(
                esp_rmaker_device_get_param_by_name(light_device, "Mode"),
                esp_rmaker_str(m_str));
        }
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

/* =====================================================
 * ROOT PAGE (Giao diện HTML giữ nguyên của bạn)
 * ===================================================== */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    /* HTML Của bạn rất tốt, tôi giữ nguyên 100% */
    const char html[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>NIGHT FURY</title>"
    "<style>"
    "body{font-family:sans-serif;background:#111;color:#eee;text-align:center}"
    ".box{background:#1c1c1c;margin:12px;padding:14px;border-radius:12px}"
    "input[type=range]{width:100%}"
    ".val{float:right;font-weight:bold}"
    ".hue{appearance:none;height:8px;border-radius:5px;"
    "background:linear-gradient(to right,red,yellow,lime,cyan,blue,magenta,red)}"
    "select{background:#333;color:#fff;border:none;padding:8px;border-radius:6px;width:60%}"
    "button{padding:8px 20px;margin:5px;border-radius:5px;border:none;"
    "background:#444;color:#fff;cursor:pointer}"
    "button:active{background:#666}"
    ".active{background:#fff !important;color:#000 !important}"
    "</style></head>"

    "<body onload='loadState()'>"
    "<h1>LIGHT CONTROL</h1>"

    "<div class='box'>"
    "<button id='btn_on' onclick='setPower(true)'>ON</button>"
    "<button id='btn_off' onclick='setPower(false)'>OFF</button>"
    "</div>"

    "<div class='box'>Brightness <span id='bri_v' class='val'></span>"
    "<input id='bri' type='range' min='0' max='100' oninput='upd(this,\"brightness\",\"bri_v\")'></div>"

    "<div class='box'>Saturation <span id='sat_v' class='val'></span>"
    "<input id='sat' type='range' min='0' max='100' oninput='upd(this,\"saturation\",\"sat_v\")'></div>"

    "<div class='box'>Mode<br>"
    "<select id='mode' onchange='post({mode:parseInt(this.value)})'>"
    "<option value='0'>Static</option>"
    "<option value='1'>Wave 1</option>"
    "<option value='2'>Wave 2</option>"
    "<option value='3'>Rainbow</option>"
    "</select></div>"

    "<div class='box'>Hue Start <span id='hs_v' class='val'></span>"
    "<input id='hs' class='hue' type='range' min='0' max='360' oninput='upd(this,\"hue_start\",\"hs_v\")'></div>"

    "<div class='box'>Hue End <span id='he_v' class='val'></span>"
    "<input id='he' class='hue' type='range' min='0' max='360' oninput='upd(this,\"hue_end\",\"he_v\")'></div>"

    "<div class='box'>Speed<br>"
    "<select id='speed' onchange='post({speed:parseInt(this.value)})'>"
    "<option value='0'>Stop</option>"
    "<option value='1'>Slow</option>"
    "<option value='2'>Medium</option>"
    "<option value='3'>Fast</option>"
    "</select></div>"

    "<script>"
    "let debounceTimer;"
    "let isDragging=false;"

    "function post(o){fetch('/set',{method:'POST',body:JSON.stringify(o)})}"
    "function setPower(v){post({power:v})}"

    "function upd(e,k,v){"
    " document.getElementById(v).innerText=e.value;"
    " isDragging=true;"
    " clearTimeout(debounceTimer);"
    " debounceTimer=setTimeout(()=>{"
    "   let o={};o[k]=parseInt(e.value);"
    "   post(o);"
    "   setTimeout(()=>{isDragging=false;},1000);"
    " },150);"
    "}"

    "function loadState(){"
    " if(isDragging) return;"
    " fetch('/state').then(r=>r.json()).then(s=>{"
    "  bri.value=s.brightness; bri_v.innerText=s.brightness;"
    "  sat.value=s.saturation; sat_v.innerText=s.saturation;"
    "  hs.value=s.hue_start; hs_v.innerText=s.hue_start;"
    "  he.value=s.hue_end; he_v.innerText=s.hue_end;"
    "  speed.value=s.speed;"
    "  mode.value=s.mode;"
    "  if(s.power){"
    "    btn_on.classList.add('active');"
    "    btn_off.classList.remove('active');"
    "  }else{"
    "    btn_off.classList.add('active');"
    "    btn_on.classList.remove('active');"
    "  }"
    " });"
    "}"
    "setInterval(loadState,3000);"
    "</script></body></html>";

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* =====================================================
 * START WEB
 * ===================================================== */
static void start_web(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;

    httpd_handle_t s;
    httpd_start(&s, &cfg);

    httpd_register_uri_handler(s, &(httpd_uri_t){"/", HTTP_GET, root_get_handler});
    httpd_register_uri_handler(s, &(httpd_uri_t){"/state", HTTP_GET, api_get_state});
    httpd_register_uri_handler(s, &(httpd_uri_t){"/set", HTTP_POST, api_set_state});
}

/* =====================================================
 * PUBLIC INIT
 * ===================================================== */
void local_control_init(void)
{
    /* Không cần Mutex vì app_effects đã tự quản lý thread-safe */
    start_web();
    ESP_LOGI(TAG, "Local Control Web Server Started");
}