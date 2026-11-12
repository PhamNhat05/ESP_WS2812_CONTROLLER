/* app_effects.h */
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Định nghĩa chung
#define LED_COUNT 8
#define TRAIN_LEN 6

// Các chế độ
enum {
    EFFECT_MODE_STATIC = 0,
    EFFECT_MODE_WAVE,
    EFFECT_MODE_WAVE2,
    EFFECT_MODE_RAINBOW,
    EFFECT_MODE_OFF = 4 // Trạng thái tắt logic
};

// Cấu trúc lưu trạng thái để báo cáo lên Cloud
typedef struct {
    bool power;
    int mode;
    int speed;
    uint16_t hue_start;
    uint16_t hue_end;
    uint16_t brightness;
    uint16_t saturation;
} app_effect_state_t;

// --- Hàm khởi tạo ---
void app_effects_init(void);
void app_effects_start_tasks(void);

// --- Hàm điều khiển (Logic) ---
// Chuyển chế độ kế tiếp (dùng cho nút bấm cứng)
void app_effects_next_mode(void);

// Lấy trạng thái hiện tại (để app_main báo cáo)
app_effect_state_t app_effects_get_state(void);

// Các hàm Setter (Dùng cho RainMaker gọi xuống)
void app_effects_set_power(bool power);
void app_effects_set_mode(int mode);
void app_effects_set_speed(int speed);
void app_effects_set_brightness(uint16_t val);
void app_effects_set_hue(uint16_t start, uint16_t end);
void app_effects_set_saturation(uint16_t sat);