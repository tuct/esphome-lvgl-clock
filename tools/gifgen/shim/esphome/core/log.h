#pragma once
// No-op logging + helper macros so lvgl_clock.cpp compiles unchanged.
#define ESP_LOGE(tag, ...) ((void) 0)
#define ESP_LOGW(tag, ...) ((void) 0)
#define ESP_LOGI(tag, ...) ((void) 0)
#define ESP_LOGD(tag, ...) ((void) 0)
#define ESP_LOGV(tag, ...) ((void) 0)
#define ESP_LOGCONFIG(tag, ...) ((void) 0)
#define YESNO(b) ((b) ? "YES" : "NO")
#define ONOFF(b) ((b) ? "ON" : "OFF")
