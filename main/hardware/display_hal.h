#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_HAL_THEME_DARK = 0,
    DISPLAY_HAL_THEME_LIGHT = 1,
} display_hal_theme_t;

typedef struct {
    const char *status_text;
    const char *title_text;
    const char *message_text;
    const char *icon_name;
    const char *voice_state;
    display_hal_theme_t theme;
} display_hal_view_t;

esp_err_t display_hal_init(void);
bool display_hal_is_ready(void);
esp_err_t display_hal_set_backend_name(const char *backend_name);
esp_err_t display_hal_render(const display_hal_view_t *view);
esp_err_t display_hal_clear(void);
esp_err_t display_hal_fill(bool on);
esp_err_t display_hal_set_invert(bool invert);
esp_err_t display_hal_set_all_on(bool on);
esp_err_t display_hal_set_power(bool on);
esp_err_t display_hal_set_contrast(uint8_t contrast);
esp_err_t display_hal_reinit(void);
esp_err_t display_hal_show_text(const char *text);
esp_err_t display_hal_set_emotion(const char *emotion_name);
esp_err_t display_hal_format_info(char *buf, size_t size);
const char *display_hal_backend_name(void);
size_t display_hal_get_width(void);
size_t display_hal_get_height(void);
size_t display_hal_get_max_lines(void);
size_t display_hal_get_max_columns(void);

#ifdef __cplusplus
}
#endif
