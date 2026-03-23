#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*button_service_toggle_cb_t)(bool enabled, void *user_ctx);
typedef void (*button_service_ptt_cb_t)(bool pressed, void *user_ctx);

esp_err_t button_service_init(void);
bool button_service_is_available(void);

esp_err_t button_service_register_toggle_cb(button_service_toggle_cb_t cb, void *user_ctx);
esp_err_t button_service_register_ptt_cb(button_service_ptt_cb_t cb, void *user_ctx);
esp_err_t button_service_set_continuous_mode(bool enabled);

bool button_service_get_continuous_mode(void);
bool button_service_get_ptt_active(void);
const char *button_service_get_last_event(void);

esp_err_t button_service_format_info(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
