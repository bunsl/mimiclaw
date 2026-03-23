#include "hardware/display_locale.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "mimi_config.h"

static const char *TAG = "display_locale";

typedef struct {
    const char *key;
    const char *value;
} builtin_locale_entry_t;

static const builtin_locale_entry_t s_builtin_zh_cn[] = {
    { "WARNING", "警告" },
    { "INFO", "信息" },
    { "ERROR", "错误" },
    { "INITIALIZING", "正在初始化..." },
    { "PLEASE_WAIT", "请稍候..." },
    { "CONNECTING", "连接中..." },
    { "CONNECTED_TO", "已连接" },
    { "SCANNING_WIFI", "扫描 Wi-Fi..." },
    { "WIFI_CONFIG_MODE", "配网模式" },
    { "ENTERING_WIFI_CONFIG_MODE", "进入配网模式..." },
    { "STANDBY", "待命" },
    { "LISTENING", "聆听中..." },
    { "SPEAKING", "说话中..." },
};

static char s_current_locale[MIMI_DISPLAY_LOCALE_LIMIT] = MIMI_DISPLAY_DEFAULT_LOCALE;
static cJSON *s_current_root = NULL;
static cJSON *s_current_strings = NULL;
static cJSON *s_fallback_root = NULL;
static cJSON *s_fallback_strings = NULL;

static void copy_locale_string(char *dst, size_t size, const char *src)
{
    if (!dst || size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, size, "%s", src);
}

static char *read_file_all(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    long file_size;

    if (out_size) {
        *out_size = 0;
    }

    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = calloc(1, (size_t)file_size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    if (file_size > 0 && fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    if (out_size) {
        *out_size = (size_t)file_size;
    }
    return buf;
}

static esp_err_t load_locale_bundle(const char *locale, cJSON **out_root, cJSON **out_strings)
{
    char path[160];
    size_t size = 0;
    char *json;
    cJSON *root;
    cJSON *strings;

    if (!locale || !locale[0] || !out_root || !out_strings) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(path, sizeof(path), MIMI_DISPLAY_LOCALE_DIR "/%s/language.json", locale);
    json = read_file_all(path, &size);
    if (!json) {
        return ESP_ERR_NOT_FOUND;
    }

    root = cJSON_ParseWithLength(json, size);
    free(json);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    strings = cJSON_GetObjectItem(root, "strings");
    if (!cJSON_IsObject(strings)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_root = root;
    *out_strings = strings;
    return ESP_OK;
}

static const char *lookup_cjson_string(cJSON *strings, const char *key)
{
    cJSON *item;

    if (!strings || !key || !key[0]) {
        return NULL;
    }

    item = cJSON_GetObjectItem(strings, key);
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) {
        return NULL;
    }

    return item->valuestring;
}

static const char *lookup_builtin_zh_cn(const char *key)
{
    size_t i;

    if (!key || !key[0]) {
        return NULL;
    }

    for (i = 0; i < (sizeof(s_builtin_zh_cn) / sizeof(s_builtin_zh_cn[0])); ++i) {
        if (strcmp(s_builtin_zh_cn[i].key, key) == 0) {
            return s_builtin_zh_cn[i].value;
        }
    }

    return NULL;
}

esp_err_t display_locale_init(const char *default_locale)
{
    esp_err_t err;
    const char *target_locale = default_locale && default_locale[0] ? default_locale : MIMI_DISPLAY_DEFAULT_LOCALE;

    if (!s_fallback_root) {
        err = load_locale_bundle("zh-CN", &s_fallback_root, &s_fallback_strings);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Fallback locale zh-CN unavailable from SPIFFS: %s", esp_err_to_name(err));
        }
    }

    return display_locale_set_current(target_locale);
}

esp_err_t display_locale_set_current(const char *locale)
{
    cJSON *root = NULL;
    cJSON *strings = NULL;
    esp_err_t err;
    const char *target = locale && locale[0] ? locale : MIMI_DISPLAY_DEFAULT_LOCALE;

    err = load_locale_bundle(target, &root, &strings);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Locale %s unavailable, keeping fallback zh-CN if possible (%s)",
                 target, esp_err_to_name(err));
        copy_locale_string(s_current_locale, sizeof(s_current_locale), target);

        if (s_current_root) {
            cJSON_Delete(s_current_root);
            s_current_root = NULL;
            s_current_strings = NULL;
        }

        if (s_fallback_strings) {
            copy_locale_string(s_current_locale, sizeof(s_current_locale), "zh-CN");
            return ESP_OK;
        }

        return err;
    }

    if (s_current_root) {
        cJSON_Delete(s_current_root);
    }
    s_current_root = root;
    s_current_strings = strings;
    copy_locale_string(s_current_locale, sizeof(s_current_locale), target);
    ESP_LOGI(TAG, "Display locale set to %s", s_current_locale);
    return ESP_OK;
}

const char *display_locale_get_current(void)
{
    return s_current_locale;
}

const char *display_locale_translate(const char *key)
{
    const char *value;

    if (!key || !key[0]) {
        return "";
    }

    value = lookup_cjson_string(s_current_strings, key);
    if (value) {
        return value;
    }

    value = lookup_cjson_string(s_fallback_strings, key);
    if (value) {
        return value;
    }

    value = lookup_builtin_zh_cn(key);
    if (value) {
        return value;
    }

    return key;
}

bool display_locale_has_key(const char *key)
{
    const char *value = display_locale_translate(key);
    return value && strcmp(value, key) != 0;
}

esp_err_t display_locale_format(char *buf, size_t size, const char *key, ...)
{
    va_list args;
    const char *fmt;
    int written;

    if (!buf || size == 0 || !key || !key[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    fmt = display_locale_translate(key);
    va_start(args, key);
    written = vsnprintf(buf, size, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= size) {
        buf[size - 1] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
