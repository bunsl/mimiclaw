#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_state_init(void);
bool display_state_is_available(void);

esp_err_t display_state_set_status_text(const char *status_text);
esp_err_t display_state_set_status_key(const char *status_key);

esp_err_t display_state_set_default_view(const char *title, const char *text, const char *icon);
esp_err_t display_state_set_default_view_key(const char *title_key, const char *message_key, const char *icon);

esp_err_t display_state_show(const char *title, const char *text, const char *icon,
                             const char *role, int duration_ms);
esp_err_t display_state_show_key(const char *title_key, const char *message_key,
                                 const char *icon, int duration_ms);
esp_err_t display_state_clear(void);

esp_err_t display_state_set_theme_name(const char *theme_name);
const char *display_state_get_theme_name(void);

esp_err_t display_state_set_locale_name(const char *locale_name);
const char *display_state_get_locale_name(void);

esp_err_t display_state_set_voice_state(const char *voice_state);
const char *display_state_get_voice_state(void);

esp_err_t display_state_format_info(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
