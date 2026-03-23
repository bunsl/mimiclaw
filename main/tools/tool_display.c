#include "tools/tool_display.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "hardware/display_hal.h"
#include "hardware/display_state.h"

static const char *get_string(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return NULL;
    }
    return item->valuestring;
}

static int get_int(cJSON *root, const char *key, int default_value)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valueint;
}

esp_err_t tool_display_show_text_execute(const char *input_json, char *output, size_t output_size)
{
    esp_err_t err;
    cJSON *root = cJSON_Parse(input_json);
    const char *title;
    const char *text;
    const char *icon;
    const char *role;
    int duration_ms;

    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    title = get_string(root, "title");
    text = get_string(root, "text");
    icon = get_string(root, "icon");
    role = get_string(root, "role");
    duration_ms = get_int(root, "duration_ms", 0);

    if (!text || !text[0]) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: 'text' required (string)");
        return ESP_ERR_INVALID_ARG;
    }

    err = display_state_show(title, text, icon, role, duration_ms);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: display_show_text failed (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size,
             "Displayed text on %s display (%ux%u, theme=%s, locale=%s, duration_ms=%d)",
             display_hal_backend_name(),
             (unsigned)display_hal_get_width(),
             (unsigned)display_hal_get_height(),
             display_state_get_theme_name(),
             display_state_get_locale_name(),
             duration_ms);
    return ESP_OK;
}

esp_err_t tool_display_clear_execute(const char *input_json, char *output, size_t output_size)
{
    esp_err_t err;
    (void)input_json;

    err = display_state_clear();
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: display_clear failed (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size, "Onboard display cleared");
    return ESP_OK;
}

esp_err_t tool_display_set_theme_execute(const char *input_json, char *output, size_t output_size)
{
    esp_err_t err;
    cJSON *root = cJSON_Parse(input_json);
    const char *theme;

    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    theme = get_string(root, "theme");
    if (!theme || !theme[0]) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: 'theme' required (dark|light)");
        return ESP_ERR_INVALID_ARG;
    }

    err = display_state_set_theme_name(theme);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: display_set_theme failed (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size, "Display theme set to %s", display_state_get_theme_name());
    return ESP_OK;
}

esp_err_t tool_display_set_locale_execute(const char *input_json, char *output, size_t output_size)
{
    esp_err_t err;
    cJSON *root = cJSON_Parse(input_json);
    const char *locale;

    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    locale = get_string(root, "locale");
    if (!locale || !locale[0]) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: 'locale' required (e.g. zh-CN)");
        return ESP_ERR_INVALID_ARG;
    }

    err = display_state_set_locale_name(locale);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: display_set_locale failed (%s)", esp_err_to_name(err));
        return err;
    }

    snprintf(output, output_size, "Display locale set to %s", display_state_get_locale_name());
    return ESP_OK;
}
