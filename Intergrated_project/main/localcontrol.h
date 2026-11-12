/* localcontrol.h */
#pragma once

#include <stdbool.h>
#include <esp_err.h>

/* ===== INIT ===== */
void local_control_init(void);

/* * Lưu ý: Không còn extern các biến g_power, g_mode... 
 * vì chúng ta sẽ lấy dữ liệu thông qua app_effects.h 
 */