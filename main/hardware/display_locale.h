#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_locale_init(const char *default_locale);
esp_err_t display_locale_set_current(const char *locale);
const char *display_locale_get_current(void);
const char *display_locale_translate(const char *key);
bool display_locale_has_key(const char *key);
esp_err_t display_locale_format(char *buf, size_t size, const char *key, ...);

#ifdef __cplusplus
}
#endif
