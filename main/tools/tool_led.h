#pragma once

#include "esp_err.h"

#include <stddef.h>

/**
 * Initialize onboard RGB LED tool resources.
 */
esp_err_t tool_led_init(void);

/**
 * Set onboard RGB LED color.
 * Input JSON: {"r": <0-255>, "g": <0-255>, "b": <0-255>}
 */
esp_err_t tool_led_set_execute(const char *input_json, char *output, size_t output_size);
