#include "hardware/button_service.h"

#include <stdio.h>
#include <string.h>

#include "button_gpio.h"
#include "button_types.h"
#include "esp_log.h"
#include "iot_button.h"
#include "mimi_config.h"

static const char *TAG = "button_service";

static button_handle_t s_button = NULL;
static bool s_available = false;
static bool s_continuous_mode = false;
static bool s_ptt_active = false;
static char s_last_event[32] = "idle";
static button_service_toggle_cb_t s_toggle_cb = NULL;
static void *s_toggle_ctx = NULL;
static button_service_ptt_cb_t s_ptt_cb = NULL;
static void *s_ptt_ctx = NULL;

static void set_last_event(const char *event_name)
{
    snprintf(s_last_event, sizeof(s_last_event), "%s", event_name ? event_name : "idle");
}

static void on_single_click(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;

    s_continuous_mode = !s_continuous_mode;
    set_last_event(s_continuous_mode ? "single_click_on" : "single_click_off");
    ESP_LOGI(TAG, "Continuous mode %s", s_continuous_mode ? "enabled" : "disabled");

    if (s_toggle_cb) {
        s_toggle_cb(s_continuous_mode, s_toggle_ctx);
    }
}

static void on_long_press_start(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;

    s_ptt_active = true;
    set_last_event("long_press_start");
    ESP_LOGI(TAG, "PTT started");

    if (s_ptt_cb) {
        s_ptt_cb(true, s_ptt_ctx);
    }
}

static void on_press_up(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;

    set_last_event("press_up");
    if (!s_ptt_active) {
        return;
    }

    s_ptt_active = false;
    ESP_LOGI(TAG, "PTT ended");
    if (s_ptt_cb) {
        s_ptt_cb(false, s_ptt_ctx);
    }
}

esp_err_t button_service_init(void)
{
    button_config_t btn_cfg = {
        .long_press_time = MIMI_BUTTON_LONG_PRESS_MS,
        .short_press_time = 180,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = MIMI_BUTTON_GPIO,
        .active_level = MIMI_BUTTON_ACTIVE_LEVEL,
        .enable_power_save = false,
        .disable_pull = false,
    };
    esp_err_t err;

    if (s_available) {
        return ESP_OK;
    }

    err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &s_button);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Button init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = iot_button_register_cb(s_button, BUTTON_SINGLE_CLICK, NULL, on_single_click, NULL);
    if (err == ESP_OK) {
        err = iot_button_register_cb(s_button, BUTTON_LONG_PRESS_START, NULL, on_long_press_start, NULL);
    }
    if (err == ESP_OK) {
        err = iot_button_register_cb(s_button, BUTTON_PRESS_UP, NULL, on_press_up, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Button callback registration failed: %s", esp_err_to_name(err));
        iot_button_delete(s_button);
        s_button = NULL;
        return err;
    }

    s_available = true;
    set_last_event("ready");
    ESP_LOGI(TAG, "Button service ready on GPIO%d", MIMI_BUTTON_GPIO);
    return ESP_OK;
}

bool button_service_is_available(void)
{
    return s_available;
}

esp_err_t button_service_register_toggle_cb(button_service_toggle_cb_t cb, void *user_ctx)
{
    s_toggle_cb = cb;
    s_toggle_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t button_service_register_ptt_cb(button_service_ptt_cb_t cb, void *user_ctx)
{
    s_ptt_cb = cb;
    s_ptt_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t button_service_set_continuous_mode(bool enabled)
{
    s_continuous_mode = enabled;
    set_last_event(enabled ? "config_on" : "config_off");
    return ESP_OK;
}

bool button_service_get_continuous_mode(void)
{
    return s_continuous_mode;
}

bool button_service_get_ptt_active(void)
{
    return s_ptt_active;
}

const char *button_service_get_last_event(void)
{
    return s_last_event;
}

esp_err_t button_service_format_info(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(buf, size,
             "available=%s gpio=%d active_level=%d continuous=%s ptt=%s last_event=%s",
             s_available ? "yes" : "no",
             MIMI_BUTTON_GPIO,
             MIMI_BUTTON_ACTIVE_LEVEL,
             s_continuous_mode ? "on" : "off",
             s_ptt_active ? "on" : "off",
             s_last_event);
    return ESP_OK;
}
