#include "tools/tool_led.h"
#include "mimi_config.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/cdefs.h>

#include "cJSON.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "tool_led";

#define LED_RMT_RESOLUTION_HZ   (10 * 1000 * 1000) /* 0.1us tick */
#define LED_RMT_MEM_SYMBOLS     64
#define LED_RMT_QUEUE_DEPTH     2

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} led_strip_encoder_t;

static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;
static bool s_led_ready = false;

static esp_err_t led_del_strip_encoder(rmt_encoder_t *encoder)
{
    led_strip_encoder_t *led = __containerof(encoder, led_strip_encoder_t, base);
    if (led->bytes_encoder) {
        rmt_del_encoder(led->bytes_encoder);
    }
    if (led->copy_encoder) {
        rmt_del_encoder(led->copy_encoder);
    }
    free(led);
    return ESP_OK;
}

static esp_err_t led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    led_strip_encoder_t *led = __containerof(encoder, led_strip_encoder_t, base);
    rmt_encoder_reset(led->bytes_encoder);
    rmt_encoder_reset(led->copy_encoder);
    led->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static size_t led_strip_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                               const void *primary_data, size_t data_size,
                               rmt_encode_state_t *ret_state)
{
    led_strip_encoder_t *led = __containerof(encoder, led_strip_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led->state) {
    case 0:
        encoded_symbols += led->bytes_encoder->encode(led->bytes_encoder, channel,
                                                       primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        /* fall through */
    case 1:
        encoded_symbols += led->copy_encoder->encode(led->copy_encoder, channel,
                                                      &led->reset_code,
                                                      sizeof(led->reset_code),
                                                      &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            state |= RMT_ENCODING_COMPLETE;
            led->state = RMT_ENCODING_RESET;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        break;
    default:
        state = RMT_ENCODING_RESET;
        break;
    }

out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t led_new_strip_encoder(rmt_encoder_handle_t *ret_encoder)
{
    if (!ret_encoder) {
        return ESP_ERR_INVALID_ARG;
    }

    led_strip_encoder_t *led = rmt_alloc_encoder_mem(sizeof(led_strip_encoder_t));
    if (!led) {
        return ESP_ERR_NO_MEM;
    }
    memset(led, 0, sizeof(*led));

    led->base.encode = led_strip_encode;
    led->base.del = led_del_strip_encoder;
    led->base.reset = led_strip_encoder_reset;

    /* WS2812 timings at 10MHz resolution: 0 -> 0.3/0.9us, 1 -> 0.9/0.3us */
    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
        .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
        .flags.msb_first = 1,
    };
    esp_err_t err = rmt_new_bytes_encoder(&bytes_cfg, &led->bytes_encoder);
    if (err != ESP_OK) {
        free(led);
        return err;
    }

    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &led->copy_encoder);
    if (err != ESP_OK) {
        rmt_del_encoder(led->bytes_encoder);
        free(led);
        return err;
    }

    /* Keep line low for 50us+ to latch the frame */
    led->reset_code = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = 250,
        .level1 = 0, .duration1 = 250,
    };

    *ret_encoder = &led->base;
    return ESP_OK;
}

static esp_err_t led_set_rgb_raw(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led_ready || !s_led_chan || !s_led_encoder) {
        return ESP_ERR_INVALID_STATE;
    }

    /* WS2812 uses GRB byte order */
    uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };

    esp_err_t err = rmt_transmit(s_led_chan, s_led_encoder, grb, sizeof(grb), &tx_cfg);
    if (err != ESP_OK) {
        return err;
    }

    return rmt_tx_wait_all_done(s_led_chan, 100);
}

esp_err_t tool_led_init(void)
{
    if (s_led_ready) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = MIMI_LED_DATA_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = LED_RMT_MEM_SYMBOLS,
        .trans_queue_depth = LED_RMT_QUEUE_DEPTH,
        .flags.with_dma = false,
    };

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    err = led_new_strip_encoder(&s_led_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_new_strip_encoder failed: %s", esp_err_to_name(err));
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
        return err;
    }

    err = rmt_enable(s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        rmt_del_encoder(s_led_encoder);
        rmt_del_channel(s_led_chan);
        s_led_encoder = NULL;
        s_led_chan = NULL;
        return err;
    }

    s_led_ready = true;
    ESP_LOGI(TAG, "Onboard RGB LED tool initialized on GPIO%d", MIMI_LED_DATA_GPIO);
    return led_set_rgb_raw(0, 0, 0);
}

static bool parse_rgb_component(cJSON *root, const char *key, uint8_t *out,
                                char *output, size_t output_size)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsNumber(item)) {
        snprintf(output, output_size, "Error: '%s' required (0-255)", key);
        return false;
    }

    int value = (int)item->valuedouble;
    if (value < 0 || value > 255) {
        snprintf(output, output_size, "Error: '%s' must be 0-255", key);
        return false;
    }

    *out = (uint8_t)value;
    return true;
}

esp_err_t tool_led_set_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (!parse_rgb_component(root, "r", &r, output, output_size) ||
        !parse_rgb_component(root, "g", &g, output, output_size) ||
        !parse_rgb_component(root, "b", &b, output, output_size)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(root);

    esp_err_t err = tool_led_init();
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: LED init failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = led_set_rgb_raw(r, g, b);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: failed to set LED (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size, "Onboard LED set to RGB(%u,%u,%u)",
             (unsigned)r, (unsigned)g, (unsigned)b);
    ESP_LOGI(TAG, "led_set: R=%u G=%u B=%u", (unsigned)r, (unsigned)g, (unsigned)b);
    return ESP_OK;
}
