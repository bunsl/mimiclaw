#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_hal_input_init(void);
bool audio_hal_input_is_available(void);
bool audio_hal_input_is_running(void);

esp_err_t audio_hal_input_start(void);
esp_err_t audio_hal_input_stop(void);
esp_err_t audio_hal_input_read_pcm16(int16_t *samples, size_t capacity,
                                     size_t *samples_read, uint32_t timeout_ms);

esp_err_t audio_hal_input_start_test(void);
esp_err_t audio_hal_input_stop_test(void);
int audio_hal_input_get_level_percent(void);
esp_err_t audio_hal_input_format_info(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
