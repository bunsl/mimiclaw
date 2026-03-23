#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_channel_init(void);
bool voice_channel_is_initialized(void);

esp_err_t voice_channel_set_continuous_mode(bool enabled);
bool voice_channel_get_continuous_mode(void);

esp_err_t voice_channel_set_ptt(bool pressed);
bool voice_channel_get_ptt_active(void);

esp_err_t voice_channel_connect_test(uint32_t timeout_ms);
esp_err_t voice_channel_reset(void);
const char *voice_channel_get_state(void);
esp_err_t voice_channel_format_info(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
