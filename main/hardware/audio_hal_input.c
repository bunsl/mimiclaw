#include "hardware/audio_hal_input.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mimi_config.h"

static const char *TAG = "audio_in";

#define AUDIO_INPUT_READ_SAMPLES 256

static i2s_chan_handle_t s_rx_handle = NULL;
static bool s_available = false;
static bool s_running = false;
static TaskHandle_t s_test_task = NULL;
static volatile bool s_test_running = false;
static volatile int s_last_level = 0;
static volatile int s_peak_level = 0;
static volatile uint32_t s_total_samples = 0;
static esp_err_t s_last_error = ESP_ERR_INVALID_STATE;

static void update_levels(const int16_t *samples, size_t count)
{
    int max_abs = 0;

    for (size_t i = 0; i < count; ++i) {
        int value = samples[i];
        if (value < 0) {
            value = -value;
        }
        if (value > max_abs) {
            max_abs = value;
        }
    }

    if (count > 0) {
        int level = (max_abs * 100) / 32767;
        s_last_level = (s_last_level * 3 + level) / 4;
        if (level > s_peak_level) {
            s_peak_level = level;
        }
        s_total_samples += (uint32_t)count;
    }
}

static esp_err_t ensure_running(void)
{
    if (!s_available) {
        return s_last_error;
    }
    if (s_running) {
        return ESP_OK;
    }
    return audio_hal_input_start();
}

static void audio_input_test_task(void *arg)
{
    int16_t samples[AUDIO_INPUT_READ_SAMPLES];

    (void)arg;
    while (s_test_running) {
        size_t samples_read = 0;
        esp_err_t err = audio_hal_input_read_pcm16(samples, AUDIO_INPUT_READ_SAMPLES,
                                                   &samples_read, 100);
        if (err == ESP_OK && samples_read > 0) {
            update_levels(samples, samples_read);
        } else if (err != ESP_ERR_TIMEOUT) {
            s_last_error = err;
            ESP_LOGW(TAG, "Mic test read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    s_test_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_hal_input_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIMI_AUDIO_INPUT_I2S_PORT, I2S_ROLE_MASTER);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIMI_AUDIO_INPUT_SAMPLE_RATE),
        .slot_cfg = {0},
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIMI_AUDIO_INPUT_SCK_GPIO,
            .ws = MIMI_AUDIO_INPUT_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIMI_AUDIO_INPUT_SD_GPIO,
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

    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.slot_cfg = slot_cfg;

    s_last_error = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (s_last_error != ESP_OK) {
        ESP_LOGW(TAG, "I2S RX channel alloc failed: %s", esp_err_to_name(s_last_error));
        return s_last_error;
    }

    s_last_error = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (s_last_error != ESP_OK) {
        ESP_LOGW(TAG, "I2S RX init failed: %s", esp_err_to_name(s_last_error));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return s_last_error;
    }

    s_available = true;
    s_last_error = ESP_OK;
    ESP_LOGI(TAG, "INMP441 input ready on I2S%d WS=%d SCK=%d SD=%d",
             MIMI_AUDIO_INPUT_I2S_PORT,
             MIMI_AUDIO_INPUT_WS_GPIO,
             MIMI_AUDIO_INPUT_SCK_GPIO,
             MIMI_AUDIO_INPUT_SD_GPIO);
    return ESP_OK;
}

bool audio_hal_input_is_available(void)
{
    return s_available;
}

bool audio_hal_input_is_running(void)
{
    return s_running;
}

esp_err_t audio_hal_input_start(void)
{
    if (!s_available) {
        return s_last_error;
    }
    if (s_running) {
        return ESP_OK;
    }

    s_last_error = i2s_channel_enable(s_rx_handle);
    if (s_last_error == ESP_OK) {
        s_running = true;
        ESP_LOGI(TAG, "Microphone capture started");
    }
    return s_last_error;
}

esp_err_t audio_hal_input_stop(void)
{
    if (!s_available) {
        return s_last_error;
    }
    if (!s_running) {
        return ESP_OK;
    }

    s_last_error = i2s_channel_disable(s_rx_handle);
    if (s_last_error == ESP_OK) {
        s_running = false;
        ESP_LOGI(TAG, "Microphone capture stopped");
    }
    return s_last_error;
}

esp_err_t audio_hal_input_read_pcm16(int16_t *samples, size_t capacity,
                                     size_t *samples_read, uint32_t timeout_ms)
{
    int32_t raw[AUDIO_INPUT_READ_SAMPLES];
    size_t bytes_read = 0;
    size_t out_count;
    esp_err_t err;

    if (!samples || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (samples_read) {
        *samples_read = 0;
    }

    err = ensure_running();
    if (err != ESP_OK) {
        return err;
    }

    out_count = capacity < AUDIO_INPUT_READ_SAMPLES ? capacity : AUDIO_INPUT_READ_SAMPLES;
    err = i2s_channel_read(s_rx_handle, raw, out_count * sizeof(int32_t), &bytes_read, timeout_ms);
    if (err != ESP_OK) {
        s_last_error = err;
        return err;
    }

    out_count = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < out_count; ++i) {
        samples[i] = (int16_t)(raw[i] >> 12);
    }

    update_levels(samples, out_count);
    if (samples_read) {
        *samples_read = out_count;
    }
    s_last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t audio_hal_input_start_test(void)
{
    esp_err_t err = audio_hal_input_start();
    if (err != ESP_OK) {
        return err;
    }
    if (s_test_task) {
        return ESP_OK;
    }

    s_test_running = true;
    if (xTaskCreate(audio_input_test_task, "mic_test", 4096, NULL, 4, &s_test_task) != pdPASS) {
        s_test_running = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t audio_hal_input_stop_test(void)
{
    s_test_running = false;
    for (int i = 0; i < 20 && s_test_task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return audio_hal_input_stop();
}

int audio_hal_input_get_level_percent(void)
{
    return s_last_level;
}

esp_err_t audio_hal_input_format_info(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(buf, size,
             "available=%s running=%s i2s=%d ws=%d sck=%d sd=%d rate=%dHz level=%d peak=%d samples=%lu last=%s",
             s_available ? "yes" : "no",
             s_running ? "yes" : "no",
             MIMI_AUDIO_INPUT_I2S_PORT,
             MIMI_AUDIO_INPUT_WS_GPIO,
             MIMI_AUDIO_INPUT_SCK_GPIO,
             MIMI_AUDIO_INPUT_SD_GPIO,
             MIMI_AUDIO_INPUT_SAMPLE_RATE,
             s_last_level,
             s_peak_level,
             (unsigned long)s_total_samples,
             esp_err_to_name(s_last_error));
    return ESP_OK;
}
