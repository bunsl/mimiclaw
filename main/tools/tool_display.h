#pragma once

#include <stddef.h>
#include "esp_err.h"

esp_err_t tool_display_show_text_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_display_clear_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_display_set_theme_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_display_set_locale_execute(const char *input_json, char *output, size_t output_size);
