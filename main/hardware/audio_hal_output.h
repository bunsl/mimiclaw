#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_hal_output_init(void);
bool audio_hal_output_is_available(void);
bool audio_hal_output_is_running(void);
int audio_hal_output_get_sample_rate(void);

esp_err_t audio_hal_output_start(void);
esp_err_t audio_hal_output_stop(void);
esp_err_t audio_hal_output_set_sample_rate(int sample_rate_hz);
esp_err_t audio_hal_output_write_pcm16(const int16_t *samples, size_t sample_count,
                                       uint32_t timeout_ms);

esp_err_t audio_hal_output_play_test_tone(int frequency_hz, int duration_ms);
esp_err_t audio_hal_output_format_info(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
