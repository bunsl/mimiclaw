#include "hardware/audio_hal_output.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mimi_config.h"

static const char *TAG = "audio_out";

#define AUDIO_OUTPUT_WRITE_SAMPLES 256

static i2s_chan_handle_t s_tx_handle = NULL;
static SemaphoreHandle_t s_lock = NULL;
static bool s_available = false;
static bool s_running = false;
static int s_sample_rate_hz = MIMI_AUDIO_OUTPUT_SAMPLE_RATE;
static esp_err_t s_last_error = ESP_ERR_INVALID_STATE;

static esp_err_t ensure_running(void)
{
    if (!s_available) {
        return s_last_error;
    }
    if (s_running) {
        return ESP_OK;
    }
    return audio_hal_output_start();
}

esp_err_t audio_hal_output_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIMI_AUDIO_OUTPUT_I2S_PORT, I2S_ROLE_MASTER);
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIMI_AUDIO_OUTPUT_SAMPLE_RATE),
        .slot_cfg = {0},
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIMI_AUDIO_OUTPUT_BCLK_GPIO,
            .ws = MIMI_AUDIO_OUTPUT_LRCK_GPIO,
            .dout = MIMI_AUDIO_OUTPUT_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (s_available) {
        return ESP_OK;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    std_cfg.slot_cfg = slot_cfg;

    s_last_error = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (s_last_error != ESP_OK) {
        ESP_LOGW(TAG, "I2S TX channel alloc failed: %s", esp_err_to_name(s_last_error));
        return s_last_error;
    }

    s_last_error = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (s_last_error != ESP_OK) {
        ESP_LOGW(TAG, "I2S TX init failed: %s", esp_err_to_name(s_last_error));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return s_last_error;
    }

    s_available = true;
    s_sample_rate_hz = MIMI_AUDIO_OUTPUT_SAMPLE_RATE;
    s_last_error = ESP_OK;
    ESP_LOGI(TAG, "MAX98357 output ready on I2S%d BCLK=%d LRCK=%d DOUT=%d rate=%dHz",
             MIMI_AUDIO_OUTPUT_I2S_PORT,
             MIMI_AUDIO_OUTPUT_BCLK_GPIO,
             MIMI_AUDIO_OUTPUT_LRCK_GPIO,
             MIMI_AUDIO_OUTPUT_DOUT_GPIO,
             s_sample_rate_hz);
    return ESP_OK;
}

bool audio_hal_output_is_available(void)
{
    return s_available;
}

bool audio_hal_output_is_running(void)
{
    return s_running;
}

int audio_hal_output_get_sample_rate(void)
{
    return s_sample_rate_hz;
}

esp_err_t audio_hal_output_start(void)
{
    if (!s_available) {
        return s_last_error;
    }
    if (s_running) {
        return ESP_OK;
    }

    s_last_error = i2s_channel_enable(s_tx_handle);
    if (s_last_error == ESP_OK) {
        s_running = true;
        ESP_LOGI(TAG, "Speaker output started");
    }
    return s_last_error;
}

esp_err_t audio_hal_output_stop(void)
{
    if (!s_available) {
        return s_last_error;
    }
    if (!s_running) {
        return ESP_OK;
    }

    s_last_error = i2s_channel_disable(s_tx_handle);
    if (s_last_error == ESP_OK) {
        s_running = false;
        ESP_LOGI(TAG, "Speaker output stopped");
    }
    return s_last_error;
}

esp_err_t audio_hal_output_set_sample_rate(int sample_rate_hz)
{
    i2s_std_clk_config_t clk_cfg;
    bool restart = false;
    esp_err_t err;

    if (!s_available) {
        return s_last_error;
    }
    if (sample_rate_hz <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_rate_hz == s_sample_rate_hz) {
        return ESP_OK;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    restart = s_running;
    if (restart) {
        err = i2s_channel_disable(s_tx_handle);
        if (err != ESP_OK) {
            s_last_error = err;
            xSemaphoreGive(s_lock);
            return err;
        }
        s_running = false;
    }

    clk_cfg = (i2s_std_clk_config_t)I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    err = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    if (err == ESP_OK) {
        s_sample_rate_hz = sample_rate_hz;
        s_last_error = ESP_OK;
        ESP_LOGI(TAG, "Speaker sample rate reconfigured to %dHz", s_sample_rate_hz);
    } else {
        s_last_error = err;
    }

    if (restart && err == ESP_OK) {
        err = i2s_channel_enable(s_tx_handle);
        if (err == ESP_OK) {
            s_running = true;
        } else {
            s_last_error = err;
        }
    }

    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t audio_hal_output_write_pcm16(const int16_t *samples, size_t sample_count,
                                       uint32_t timeout_ms)
{
    int16_t stereo[AUDIO_OUTPUT_WRITE_SAMPLES * 2];
    size_t offset = 0;
    esp_err_t err;

    if (!samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ensure_running();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    while (offset < sample_count) {
        size_t frames = sample_count - offset;
        size_t bytes_written = 0;

        if (frames > AUDIO_OUTPUT_WRITE_SAMPLES) {
            frames = AUDIO_OUTPUT_WRITE_SAMPLES;
        }

        for (size_t i = 0; i < frames; ++i) {
            stereo[i * 2] = samples[offset + i];
            stereo[i * 2 + 1] = samples[offset + i];
        }

        err = i2s_channel_write(s_tx_handle, stereo,
                                frames * 2 * sizeof(int16_t),
                                &bytes_written, timeout_ms);
        if (err != ESP_OK) {
            s_last_error = err;
            xSemaphoreGive(s_lock);
            return err;
        }
        offset += bytes_written / (2 * sizeof(int16_t));
    }

    s_last_error = ESP_OK;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t audio_hal_output_play_test_tone(int frequency_hz, int duration_ms)
{
    int16_t chunk[AUDIO_OUTPUT_WRITE_SAMPLES];
    size_t total_samples;
    size_t generated = 0;
    int amplitude = 9000;
    int period;
    esp_err_t err;

    if (frequency_hz <= 0 || duration_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ensure_running();
    if (err != ESP_OK) {
        return err;
    }

    total_samples = ((size_t)s_sample_rate_hz * (size_t)duration_ms) / 1000U;
    period = s_sample_rate_hz / frequency_hz;
    if (period < 2) {
        period = 2;
    }

    while (generated < total_samples) {
        size_t frames = total_samples - generated;
        if (frames > AUDIO_OUTPUT_WRITE_SAMPLES) {
            frames = AUDIO_OUTPUT_WRITE_SAMPLES;
        }

        for (size_t i = 0; i < frames; ++i) {
            size_t position = generated + i;
            bool high = (position % (size_t)period) < ((size_t)period / 2U);
            chunk[i] = high ? amplitude : -amplitude;
        }

        err = audio_hal_output_write_pcm16(chunk, frames, 100);
        if (err != ESP_OK) {
            return err;
        }
        generated += frames;
    }

    return ESP_OK;
}

esp_err_t audio_hal_output_format_info(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(buf, size,
             "available=%s running=%s i2s=%d bclk=%d lrck=%d dout=%d rate=%dHz last=%s",
             s_available ? "yes" : "no",
             s_running ? "yes" : "no",
             MIMI_AUDIO_OUTPUT_I2S_PORT,
             MIMI_AUDIO_OUTPUT_BCLK_GPIO,
             MIMI_AUDIO_OUTPUT_LRCK_GPIO,
             MIMI_AUDIO_OUTPUT_DOUT_GPIO,
             s_sample_rate_hz,
             esp_err_to_name(s_last_error));
    return ESP_OK;
}
