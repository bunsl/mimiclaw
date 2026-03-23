#include "hardware/display_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hardware/display_hal.h"
#include "hardware/display_locale.h"
#include "mimi_config.h"
#include "nvs.h"

static const char *TAG = "display_state";

#define DISPLAY_KEY_LIMIT 32

typedef struct {
    char status_key[DISPLAY_KEY_LIMIT];
    char status_text[MIMI_DISPLAY_STATUS_LIMIT + 1];
    char title_key[DISPLAY_KEY_LIMIT];
    char title_text[MIMI_DISPLAY_TITLE_LIMIT + 1];
    char message_key[DISPLAY_KEY_LIMIT];
    char message_text[MIMI_DISPLAY_TEXT_LIMIT + 1];
    char icon[MIMI_DISPLAY_ICON_LIMIT + 1];
} display_content_t;

static SemaphoreHandle_t s_lock = NULL;
static esp_timer_handle_t s_overlay_timer = NULL;
static bool s_initialized = false;
static bool s_display_available = false;
static esp_err_t s_last_display_err = ESP_ERR_INVALID_STATE;
static int s_theme = MIMI_DISPLAY_THEME_DARK;
static char s_theme_name[16] = MIMI_DISPLAY_DEFAULT_THEME;
static char s_locale[MIMI_DISPLAY_LOCALE_LIMIT] = MIMI_DISPLAY_DEFAULT_LOCALE;
static char s_voice_state[MIMI_DISPLAY_VOICE_STATE_LIMIT] = "idle";
static display_content_t s_base = {0};
static display_content_t s_overlay = {0};
static bool s_overlay_active = false;

static void copy_text(char *dst, size_t size, const char *src)
{
    if (!dst || size == 0) {
        return;
    }
    snprintf(dst, size, "%s", src ? src : "");
}

static void clear_content(display_content_t *content)
{
    if (!content) {
        return;
    }
    memset(content, 0, sizeof(*content));
}

static const char *translate_or_text(const char *key, const char *text)
{
    if (key && key[0]) {
        return display_locale_translate(key);
    }
    return text ? text : "";
}

static const char *role_default_icon(const char *role)
{
    if (!role || !role[0]) {
        return "microchip_ai";
    }
    if (strcmp(role, "user") == 0) {
        return "user";
    }
    if (strcmp(role, "error") == 0) {
        return "triangle_exclamation";
    }
    if (strcmp(role, "system") == 0 || strcmp(role, "tool") == 0) {
        return "circle_info";
    }
    return "microchip_ai";
}

static esp_err_t read_effective_str(const char *ns, const char *key,
                                    const char *build_value, const char *default_value,
                                    char *buf, size_t size)
{
    nvs_handle_t nvs;
    size_t len;

    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    buf[0] = '\0';
    if (nvs_open(ns, NVS_READONLY, &nvs) == ESP_OK) {
        len = size;
        if (nvs_get_str(nvs, key, buf, &len) == ESP_OK && buf[0]) {
            nvs_close(nvs);
            return ESP_OK;
        }
        nvs_close(nvs);
    }

    if (build_value && build_value[0]) {
        copy_text(buf, size, build_value);
    } else if (default_value && default_value[0]) {
        copy_text(buf, size, default_value);
    }
    return ESP_OK;
}

static esp_err_t persist_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    if (!value || !value[0]) {
        err = nvs_erase_key(nvs, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        err = nvs_set_str(nvs, key, value);
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static int theme_from_name(const char *theme_name)
{
    if (theme_name && strcmp(theme_name, "light") == 0) {
        return MIMI_DISPLAY_THEME_LIGHT;
    }
    return MIMI_DISPLAY_THEME_DARK;
}

static const char *normalize_locale_name(const char *locale_name)
{
    if (!locale_name || !locale_name[0]) {
        return MIMI_DISPLAY_DEFAULT_LOCALE;
    }
    return locale_name;
}

static const char *overlay_icon_or_default(const display_content_t *active)
{
    if (active && active->icon[0]) {
        return active->icon;
    }
    if (s_base.icon[0]) {
        return s_base.icon;
    }
    return "microchip_ai";
}

static esp_err_t apply_locked(void)
{
    const display_content_t *active = s_overlay_active ? &s_overlay : &s_base;
    display_hal_view_t view;

    if (!s_display_available) {
        return s_last_display_err;
    }

    memset(&view, 0, sizeof(view));
    view.status_text = translate_or_text(s_base.status_key, s_base.status_text);
    view.title_text = translate_or_text(active->title_key, active->title_text);
    view.message_text = translate_or_text(active->message_key, active->message_text);
    view.icon_name = overlay_icon_or_default(active);
    view.voice_state = s_voice_state;
    view.theme = (display_hal_theme_t)s_theme;

    s_last_display_err = display_hal_render(&view);
    return s_last_display_err;
}

static void overlay_timeout_cb(void *arg)
{
    (void)arg;

    if (!s_lock) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    s_overlay_active = false;
    clear_content(&s_overlay);
    (void)apply_locked();
    xSemaphoreGive(s_lock);
}

static esp_err_t ensure_initialized(void)
{
    if (!s_initialized) {
        return display_state_init();
    }
    return ESP_OK;
}

static esp_err_t set_theme_locked(const char *theme_name, bool persist)
{
    const char *name = (theme_name && theme_name[0]) ? theme_name : MIMI_DISPLAY_DEFAULT_THEME;
    int theme = theme_from_name(name);
    esp_err_t err = ESP_OK;

    s_theme = theme;
    copy_text(s_theme_name, sizeof(s_theme_name),
              theme == MIMI_DISPLAY_THEME_LIGHT ? "light" : "dark");

    if (persist) {
        err = persist_str(MIMI_NVS_DISPLAY, MIMI_NVS_KEY_DISPLAY_THEME, s_theme_name);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_display_available) {
        err = apply_locked();
    }
    return err;
}

static esp_err_t set_locale_locked(const char *locale_name, bool persist)
{
    const char *name = normalize_locale_name(locale_name);
    esp_err_t err = display_locale_set_current(name);

    if (err == ESP_OK) {
        copy_text(s_locale, sizeof(s_locale), display_locale_get_current());
    } else {
        copy_text(s_locale, sizeof(s_locale), name);
    }

    if (persist) {
        esp_err_t save_err = persist_str(MIMI_NVS_DISPLAY, MIMI_NVS_KEY_DISPLAY_LOCALE, s_locale);
        if (save_err != ESP_OK) {
            return save_err;
        }
    }

    if (s_display_available) {
        err = apply_locked();
    }
    return err;
}

esp_err_t display_state_init(void)
{
    char theme_name[16];
    char locale_name[MIMI_DISPLAY_LOCALE_LIMIT];
    esp_err_t err;

    if (s_initialized) {
        return s_last_display_err;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_overlay_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = overlay_timeout_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "display_overlay",
            .skip_unhandled_events = true,
        };
        err = esp_timer_create(&timer_args, &s_overlay_timer);
        if (err != ESP_OK) {
            return err;
        }
    }

    read_effective_str(MIMI_NVS_DISPLAY, MIMI_NVS_KEY_DISPLAY_THEME,
                       MIMI_SECRET_DISPLAY_THEME, MIMI_DISPLAY_DEFAULT_THEME,
                       theme_name, sizeof(theme_name));
    read_effective_str(MIMI_NVS_DISPLAY, MIMI_NVS_KEY_DISPLAY_LOCALE,
                       MIMI_SECRET_DISPLAY_LOCALE, MIMI_DISPLAY_DEFAULT_LOCALE,
                       locale_name, sizeof(locale_name));

    err = display_locale_init(locale_name);
    if (err == ESP_OK) {
        copy_text(s_locale, sizeof(s_locale), display_locale_get_current());
    } else {
        copy_text(s_locale, sizeof(s_locale), locale_name);
    }

    clear_content(&s_base);
    clear_content(&s_overlay);
    copy_text(s_voice_state, sizeof(s_voice_state), "idle");
    copy_text(s_base.status_text, sizeof(s_base.status_text), "MimiClaw");
    copy_text(s_base.title_key, sizeof(s_base.title_key), "INITIALIZING");
    copy_text(s_base.message_key, sizeof(s_base.message_key), "PLEASE_WAIT");
    copy_text(s_base.icon, sizeof(s_base.icon), "microchip_ai");

    s_theme = theme_from_name(theme_name);
    copy_text(s_theme_name, sizeof(s_theme_name),
              s_theme == MIMI_DISPLAY_THEME_LIGHT ? "light" : "dark");

#if MIMI_DIAG_DISABLE_DISPLAY
    s_last_display_err = ESP_ERR_NOT_SUPPORTED;
    s_display_available = false;
    s_initialized = true;
    ESP_LOGW(TAG, "Display disabled by diagnostic override");
    return s_last_display_err;
#endif

    s_last_display_err = display_hal_init();
    s_display_available = (s_last_display_err == ESP_OK);
    if (!s_display_available) {
        ESP_LOGW(TAG, "Display unavailable: %s", esp_err_to_name(s_last_display_err));
    }

    s_initialized = true;

    return s_last_display_err;
}

bool display_state_is_available(void)
{
    return s_display_available;
}

esp_err_t display_state_set_status_text(const char *status_text)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_base.status_key[0] = '\0';
    copy_text(s_base.status_text, sizeof(s_base.status_text), status_text);
    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_set_status_key(const char *status_key)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    copy_text(s_base.status_key, sizeof(s_base.status_key), status_key);
    s_base.status_text[0] = '\0';
    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_set_default_view(const char *title, const char *text, const char *icon)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_overlay_active = false;
    if (s_overlay_timer) {
        (void)esp_timer_stop(s_overlay_timer);
    }
    clear_content(&s_overlay);
    s_base.title_key[0] = '\0';
    s_base.message_key[0] = '\0';
    copy_text(s_base.title_text, sizeof(s_base.title_text), title);
    copy_text(s_base.message_text, sizeof(s_base.message_text), text);
    copy_text(s_base.icon, sizeof(s_base.icon), icon);
    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_set_default_view_key(const char *title_key, const char *message_key, const char *icon)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_overlay_active = false;
    if (s_overlay_timer) {
        (void)esp_timer_stop(s_overlay_timer);
    }
    clear_content(&s_overlay);
    copy_text(s_base.title_key, sizeof(s_base.title_key), title_key);
    copy_text(s_base.message_key, sizeof(s_base.message_key), message_key);
    s_base.title_text[0] = '\0';
    s_base.message_text[0] = '\0';
    copy_text(s_base.icon, sizeof(s_base.icon), icon);
    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_show(const char *title, const char *text, const char *icon,
                             const char *role, int duration_ms)
{
    esp_err_t err = ensure_initialized();
    const char *resolved_icon = (icon && icon[0]) ? icon : role_default_icon(role);

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (duration_ms > 0) {
        clear_content(&s_overlay);
        copy_text(s_overlay.title_text, sizeof(s_overlay.title_text), title);
        copy_text(s_overlay.message_text, sizeof(s_overlay.message_text), text);
        copy_text(s_overlay.icon, sizeof(s_overlay.icon), resolved_icon);
        s_overlay_active = true;
        if (s_overlay_timer) {
            (void)esp_timer_stop(s_overlay_timer);
            (void)esp_timer_start_once(s_overlay_timer, (uint64_t)duration_ms * 1000ULL);
        }
    } else {
        s_overlay_active = false;
        if (s_overlay_timer) {
            (void)esp_timer_stop(s_overlay_timer);
        }
        clear_content(&s_overlay);
        s_base.title_key[0] = '\0';
        s_base.message_key[0] = '\0';
        copy_text(s_base.title_text, sizeof(s_base.title_text), title);
        copy_text(s_base.message_text, sizeof(s_base.message_text), text);
        copy_text(s_base.icon, sizeof(s_base.icon), resolved_icon);
    }

    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_show_key(const char *title_key, const char *message_key,
                                 const char *icon, int duration_ms)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (duration_ms > 0) {
        clear_content(&s_overlay);
        copy_text(s_overlay.title_key, sizeof(s_overlay.title_key), title_key);
        copy_text(s_overlay.message_key, sizeof(s_overlay.message_key), message_key);
        copy_text(s_overlay.icon, sizeof(s_overlay.icon), icon);
        s_overlay_active = true;
        if (s_overlay_timer) {
            (void)esp_timer_stop(s_overlay_timer);
            (void)esp_timer_start_once(s_overlay_timer, (uint64_t)duration_ms * 1000ULL);
        }
    } else {
        s_overlay_active = false;
        if (s_overlay_timer) {
            (void)esp_timer_stop(s_overlay_timer);
        }
        clear_content(&s_overlay);
        copy_text(s_base.title_key, sizeof(s_base.title_key), title_key);
        copy_text(s_base.message_key, sizeof(s_base.message_key), message_key);
        s_base.title_text[0] = '\0';
        s_base.message_text[0] = '\0';
        copy_text(s_base.icon, sizeof(s_base.icon), icon);
    }

    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_clear(void)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_overlay_active = false;
    if (s_overlay_timer) {
        (void)esp_timer_stop(s_overlay_timer);
    }
    clear_content(&s_overlay);
    s_base.title_key[0] = '\0';
    s_base.message_key[0] = '\0';
    s_base.title_text[0] = '\0';
    s_base.message_text[0] = '\0';
    copy_text(s_base.icon, sizeof(s_base.icon), "microchip_ai");
    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t display_state_set_theme_name(const char *theme_name)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    err = set_theme_locked(theme_name, true);
    xSemaphoreGive(s_lock);
    return err;
}

const char *display_state_get_theme_name(void)
{
    return s_theme_name;
}

esp_err_t display_state_set_locale_name(const char *locale_name)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    err = set_locale_locked(locale_name, true);
    xSemaphoreGive(s_lock);
    return err;
}

const char *display_state_get_locale_name(void)
{
    return s_locale;
}

esp_err_t display_state_set_voice_state(const char *voice_state)
{
    esp_err_t err = ensure_initialized();

    if (err != ESP_OK && !s_initialized) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    copy_text(s_voice_state, sizeof(s_voice_state), voice_state ? voice_state : "idle");
    err = apply_locked();
    xSemaphoreGive(s_lock);
    return err;
}

const char *display_state_get_voice_state(void)
{
    return s_voice_state;
}

esp_err_t display_state_format_info(char *buf, size_t size)
{
    char hal_info[384] = {0};

    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    display_hal_format_info(hal_info, sizeof(hal_info));
    snprintf(buf, size,
             "%s theme=%s locale=%s voice=%s overlay=%s display=%s",
             hal_info,
             s_theme_name,
             s_locale,
             s_voice_state,
             s_overlay_active ? "on" : "off",
             s_display_available ? "ready" : "unavailable");
    return ESP_OK;
}
