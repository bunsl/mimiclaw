#include "hardware/display_hal.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fonts/font_awesome.h"
#include "lvgl.h"
#include "mimi_config.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "display_hal";

#define DISPLAY_PROBE_TIMEOUT_MS          100
#define DISPLAY_LVGL_LOCK_TIMEOUT_MS      1000
#define DISPLAY_STATUS_MAX_LEN            MIMI_DISPLAY_STATUS_LIMIT
#define DISPLAY_MESSAGE_MAX_LEN           MIMI_DISPLAY_TEXT_LIMIT
#define DISPLAY_COMPOSED_TEXT_MAX         256
#define DISPLAY_EMOTION_NAME_MAX          32
#define DISPLAY_OLED_COLUMNS              10
#define DISPLAY_ST7735_COLUMNS            18
#define DISPLAY_OLED_LINES                2
#define DISPLAY_ST7735_LINES              6
#define DISPLAY_OLED_ICON_LANE_WIDTH      32
#define DISPLAY_ST7735_ICON_LANE_WIDTH    40
#define DISPLAY_OLED_STATUS_BAR_HEIGHT    16
#define DISPLAY_ST7735_STATUS_BAR_HEIGHT  22
#define DISPLAY_STATUS_ICON_SPACE         40

#define OLED_CMD_SET_CONTRAST             0x81
#define OLED_CMD_DISPLAY_ALL_OFF          0xA4
#define OLED_CMD_DISPLAY_ALL_ON           0xA5
#define OLED_CMD_NORMAL_DISPLAY           0xA6
#define OLED_CMD_INVERSE_DISPLAY          0xA7

#define ST7735_CMD_FRMCTR1                0xB1
#define ST7735_CMD_FRMCTR2                0xB2
#define ST7735_CMD_FRMCTR3                0xB3
#define ST7735_CMD_INVCTR                 0xB4
#define ST7735_CMD_PWCTR1                 0xC0
#define ST7735_CMD_PWCTR2                 0xC1
#define ST7735_CMD_PWCTR3                 0xC2
#define ST7735_CMD_PWCTR4                 0xC3
#define ST7735_CMD_PWCTR5                 0xC4
#define ST7735_CMD_VMCTR1                 0xC5
#define ST7735_CMD_GMCTRP1                0xE0
#define ST7735_CMD_GMCTRN1                0xE1

typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    size_t data_size;
    uint16_t delay_ms;
} st7735_init_cmd_t;

static const uint8_t s_st7735_colmod_16bit[] = {0x05};
static const uint8_t s_st7735_frmctr1[] = {0x01, 0x2C, 0x2D};
static const uint8_t s_st7735_frmctr2[] = {0x01, 0x2C, 0x2D};
static const uint8_t s_st7735_frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
static const uint8_t s_st7735_invctr[] = {0x07};
static const uint8_t s_st7735_pwctr1[] = {0xA2, 0x02, 0x84};
static const uint8_t s_st7735_pwctr2[] = {0xC5};
static const uint8_t s_st7735_pwctr3[] = {0x0A, 0x00};
static const uint8_t s_st7735_pwctr4[] = {0x8A, 0x2A};
static const uint8_t s_st7735_pwctr5[] = {0x8A, 0xEE};
static const uint8_t s_st7735_vmctr1[] = {0x0E};
static const uint8_t s_st7735_gmctrp1[] = {
    0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
};
static const uint8_t s_st7735_gmctrn1[] = {
    0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
};

static const st7735_init_cmd_t s_st7735_init_cmds[] = {
    {LCD_CMD_SWRESET, NULL, 0, 150},
    {LCD_CMD_SLPOUT, NULL, 0, 120},
    {LCD_CMD_COLMOD, s_st7735_colmod_16bit, sizeof(s_st7735_colmod_16bit), 10},
    {ST7735_CMD_FRMCTR1, s_st7735_frmctr1, sizeof(s_st7735_frmctr1), 0},
    {ST7735_CMD_FRMCTR2, s_st7735_frmctr2, sizeof(s_st7735_frmctr2), 0},
    {ST7735_CMD_FRMCTR3, s_st7735_frmctr3, sizeof(s_st7735_frmctr3), 0},
    {ST7735_CMD_INVCTR, s_st7735_invctr, sizeof(s_st7735_invctr), 0},
    {ST7735_CMD_PWCTR1, s_st7735_pwctr1, sizeof(s_st7735_pwctr1), 0},
    {ST7735_CMD_PWCTR2, s_st7735_pwctr2, sizeof(s_st7735_pwctr2), 0},
    {ST7735_CMD_PWCTR3, s_st7735_pwctr3, sizeof(s_st7735_pwctr3), 0},
    {ST7735_CMD_PWCTR4, s_st7735_pwctr4, sizeof(s_st7735_pwctr4), 0},
    {ST7735_CMD_PWCTR5, s_st7735_pwctr5, sizeof(s_st7735_pwctr5), 0},
    {ST7735_CMD_VMCTR1, s_st7735_vmctr1, sizeof(s_st7735_vmctr1), 0},
    {ST7735_CMD_GMCTRP1, s_st7735_gmctrp1, sizeof(s_st7735_gmctrp1), 0},
    {ST7735_CMD_GMCTRN1, s_st7735_gmctrn1, sizeof(s_st7735_gmctrn1), 0},
    {LCD_CMD_NORON, NULL, 0, 10},
    {LCD_CMD_DISPON, NULL, 0, 100},
};

typedef struct {
    lv_color_t bg;
    lv_color_t surface;
    lv_color_t accent;
    lv_color_t text;
    lv_color_t accent_text;
    lv_color_t muted;
    lv_color_t icon_lane;
} display_palette_t;

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(BUILTIN_LARGE_ICON_FONT);

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static bool s_spi_bus_initialized = false;
static bool s_backlight_ready = false;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_display = NULL;
static bool s_lvgl_initialized = false;
static bool s_ready = false;
static bool s_power_on = true;
static bool s_inverted = false;
static bool s_all_on = false;
static uint8_t s_contrast = 0x7F;
static int s_requested_backend = MIMI_DISPLAY_BACKEND;
static int s_active_backend = MIMI_DISPLAY_BACKEND_NONE;
static esp_err_t s_last_init_result = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_bus_result = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_probe_cfg_result = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_probe_0x3c_result = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_probe_0x3d_result = ESP_ERR_INVALID_STATE;
static display_hal_theme_t s_theme = DISPLAY_HAL_THEME_DARK;

static lv_obj_t *s_container = NULL;
static lv_obj_t *s_content_box = NULL;
static lv_obj_t *s_emotion_label = NULL;
static lv_obj_t *s_sidebar = NULL;
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_icons_box = NULL;
static lv_obj_t *s_mute_label = NULL;
static lv_obj_t *s_network_label = NULL;
static lv_obj_t *s_battery_label = NULL;
static lv_obj_t *s_message_label = NULL;

static char s_status_text[DISPLAY_STATUS_MAX_LEN + 1] = "MimiClaw";
static char s_title_text[MIMI_DISPLAY_TITLE_LIMIT + 1] = "";
static char s_message_text[DISPLAY_MESSAGE_MAX_LEN + 1] = "MimiClaw";
static char s_composed_text[DISPLAY_COMPOSED_TEXT_MAX + 1] = "MimiClaw";
static char s_icon_name[DISPLAY_EMOTION_NAME_MAX + 1] = "microchip_ai";
static char s_voice_state[MIMI_DISPLAY_VOICE_STATE_LIMIT] = "idle";

static void teardown_display(void);
static esp_err_t ensure_ready(void);

static bool gpio_is_assigned(int gpio_num)
{
    return gpio_num >= 0;
}

static bool backend_is_oled(int backend)
{
    return backend == MIMI_DISPLAY_BACKEND_SSD1306 ||
           backend == MIMI_DISPLAY_BACKEND_SH1106;
}

static bool backend_is_color_tft(int backend)
{
    return backend == MIMI_DISPLAY_BACKEND_ST7735;
}

static const char *backend_to_name(int backend)
{
    switch (backend) {
    case MIMI_DISPLAY_BACKEND_SSD1306:
        return "ssd1306";
    case MIMI_DISPLAY_BACKEND_SH1106:
        return "sh1106";
    case MIMI_DISPLAY_BACKEND_ST7735:
        return "st7735";
    case MIMI_DISPLAY_BACKEND_NONE:
    default:
        return "none";
    }
}

static const char *layout_name(int backend)
{
    if (backend_is_color_tft(backend)) {
        return "tft-128x128";
    }
    if (backend_is_oled(backend)) {
        return "xiaozhi-128x32";
    }
    return "none";
}

static int default_backend(void)
{
    return MIMI_DISPLAY_BACKEND;
}

static int metrics_backend(void)
{
    if (s_active_backend != MIMI_DISPLAY_BACKEND_NONE) {
        return s_active_backend;
    }
    return s_requested_backend;
}

static bool is_supported_backend(int backend)
{
    return backend == MIMI_DISPLAY_BACKEND_SSD1306 ||
           backend == MIMI_DISPLAY_BACKEND_ST7735;
}

static size_t backend_width(int backend)
{
    if (backend_is_color_tft(backend)) {
        return MIMI_ST7735_WIDTH;
    }
    if (backend_is_oled(backend)) {
        return MIMI_OLED_WIDTH;
    }
    return 0;
}

static size_t backend_height(int backend)
{
    if (backend_is_color_tft(backend)) {
        return MIMI_ST7735_HEIGHT;
    }
    if (backend_is_oled(backend)) {
        return MIMI_OLED_HEIGHT;
    }
    return 0;
}

static size_t backend_max_lines(int backend)
{
    return backend_is_color_tft(backend) ? DISPLAY_ST7735_LINES : DISPLAY_OLED_LINES;
}

static size_t backend_max_columns(int backend)
{
    return backend_is_color_tft(backend) ? DISPLAY_ST7735_COLUMNS : DISPLAY_OLED_COLUMNS;
}

static int backend_icon_lane_width(int backend)
{
    return backend_is_color_tft(backend) ? DISPLAY_ST7735_ICON_LANE_WIDTH
                                         : DISPLAY_OLED_ICON_LANE_WIDTH;
}

static int backend_status_bar_height(int backend)
{
    return backend_is_color_tft(backend) ? DISPLAY_ST7735_STATUS_BAR_HEIGHT
                                         : DISPLAY_OLED_STATUS_BAR_HEIGHT;
}

static int backend_sidebar_width(int backend)
{
    int width = (int)backend_width(backend);
    int icon_width = backend_icon_lane_width(backend);

    return (width > icon_width) ? (width - icon_width) : width;
}

static bool st7735_pins_ready(void)
{
    return gpio_is_assigned(MIMI_ST7735_MOSI_GPIO) &&
           gpio_is_assigned(MIMI_ST7735_CLK_GPIO) &&
           gpio_is_assigned(MIMI_ST7735_DC_GPIO) &&
           gpio_is_assigned(MIMI_ST7735_CS_GPIO);
}

static bool st7735_cs_conflicts_with_led(void)
{
    return gpio_is_assigned(MIMI_ST7735_CS_GPIO) &&
           MIMI_ST7735_CS_GPIO == MIMI_LED_DATA_GPIO;
}

static int st7735_backlight_on_level(void)
{
    return MIMI_ST7735_BACKLIGHT_INVERT ? 0 : 1;
}

static int st7735_backlight_off_level(void)
{
    return MIMI_ST7735_BACKLIGHT_INVERT ? 1 : 0;
}

static display_palette_t palette_for_theme(display_hal_theme_t theme, bool monochrome)
{
    if (monochrome) {
        display_palette_t mono = {
            .bg = lv_color_black(),
            .surface = lv_color_black(),
            .accent = lv_color_black(),
            .text = lv_color_white(),
            .accent_text = lv_color_white(),
            .muted = lv_color_white(),
            .icon_lane = lv_color_black(),
        };
        return mono;
    }

    if (theme == DISPLAY_HAL_THEME_LIGHT) {
        display_palette_t light = {
            .bg = lv_color_hex(0xEEF2F6),
            .surface = lv_color_hex(0xFFFFFF),
            .accent = lv_color_hex(0x2866E3),
            .text = lv_color_hex(0x162030),
            .accent_text = lv_color_white(),
            .muted = lv_color_hex(0x5E7389),
            .icon_lane = lv_color_hex(0x3C7AF0),
        };
        return light;
    }

    display_palette_t dark = {
        .bg = lv_color_hex(0x07121F),
        .surface = lv_color_hex(0x102338),
        .accent = lv_color_hex(0x1A9CD8),
        .text = lv_color_hex(0xEFF5FA),
        .accent_text = lv_color_white(),
        .muted = lv_color_hex(0x8FA7BC),
        .icon_lane = lv_color_hex(0x12486A),
    };
    return dark;
}

static esp_err_t display_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lvgl_port_lock(timeout_ms)) {
        ESP_LOGE(TAG, "LVGL lock timeout after %lu ms", (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void display_unlock(void)
{
    if (s_lvgl_initialized) {
        lvgl_port_unlock();
    }
}

static void clear_ui_handles(void)
{
    s_container = NULL;
    s_content_box = NULL;
    s_emotion_label = NULL;
    s_sidebar = NULL;
    s_status_bar = NULL;
    s_status_label = NULL;
    s_icons_box = NULL;
    s_mute_label = NULL;
    s_network_label = NULL;
    s_battery_label = NULL;
    s_message_label = NULL;
}

static void flat_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static const char *current_main_icon(void)
{
    (void)s_icon_name;
    return "AI";
}

static const char *voice_state_icon(void)
{
    if (strcmp(s_voice_state, "connecting") == 0) {
        return FONT_AWESOME_SPINNER;
    }
    if (strcmp(s_voice_state, "listening_ptt") == 0 ||
        strcmp(s_voice_state, "listening_toggle") == 0) {
        return FONT_AWESOME_MICROPHONE;
    }
    if (strcmp(s_voice_state, "thinking") == 0) {
        return FONT_AWESOME_COMMENT_QUESTION;
    }
    if (strcmp(s_voice_state, "speaking") == 0) {
        return FONT_AWESOME_VOLUME_HIGH;
    }
    if (strcmp(s_voice_state, "error") == 0) {
        return FONT_AWESOME_TRIANGLE_EXCLAMATION;
    }
    return "";
}

static const char *theme_state_icon(void)
{
    return (s_theme == DISPLAY_HAL_THEME_LIGHT) ? FONT_AWESOME_SUN : FONT_AWESOME_MOON;
}

static void copy_segment_sanitized(const char *start, size_t len, char *dst, size_t dst_size)
{
    size_t out = 0;

    if (!dst || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (!start) {
        return;
    }

    for (size_t i = 0; i < len && start[i] != '\0' && out + 1 < dst_size; ++i) {
        char c = start[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
        dst[out++] = c;
    }
    dst[out] = '\0';
}

static void copy_full_sanitized(const char *src, char *dst, size_t dst_size)
{
    copy_segment_sanitized(src, src ? strlen(src) : 0, dst, dst_size);
}

static void append_full_sanitized(char *dst, size_t dst_size, const char *src)
{
    size_t used;

    if (!dst || dst_size == 0 || !src || !src[0]) {
        return;
    }

    used = strlen(dst);
    if (used >= dst_size - 1) {
        return;
    }

    copy_segment_sanitized(src, strlen(src), dst + used, dst_size - used);
}

static void build_composed_text(void)
{
    s_composed_text[0] = '\0';

    if (s_title_text[0]) {
        append_full_sanitized(s_composed_text, sizeof(s_composed_text), s_title_text);
    }
    if (s_title_text[0] && s_message_text[0]) {
        append_full_sanitized(s_composed_text, sizeof(s_composed_text), "\n");
    }
    if (s_message_text[0]) {
        append_full_sanitized(s_composed_text, sizeof(s_composed_text), s_message_text);
    }
}

static void reset_probe_state(void)
{
    s_last_bus_result = ESP_ERR_INVALID_STATE;
    s_last_probe_cfg_result = ESP_ERR_NOT_SUPPORTED;
    s_last_probe_0x3c_result = ESP_ERR_NOT_SUPPORTED;
    s_last_probe_0x3d_result = ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_tx_cmd(uint8_t cmd)
{
    if (!s_panel_io) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(s_panel_io, cmd, NULL, 0);
}

static esp_err_t panel_tx_cmd_u8(uint8_t cmd, uint8_t value)
{
    if (!s_panel_io) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(s_panel_io, cmd, &value, 1);
}

static uint8_t st7735_initial_madctl(void)
{
    uint8_t madctl = 0;

    if (MIMI_ST7735_RGB_ORDER == LCD_RGB_ELEMENT_ORDER_BGR) {
        madctl |= LCD_CMD_BGR_BIT;
    }
    return madctl;
}

static esp_err_t st7735_run_init_sequence(void)
{
    size_t i;

    if (!s_panel_io) {
        return ESP_ERR_INVALID_STATE;
    }

    for (i = 0; i < (sizeof(s_st7735_init_cmds) / sizeof(s_st7735_init_cmds[0])); ++i) {
        const st7735_init_cmd_t *cmd = &s_st7735_init_cmds[i];
        esp_err_t ret = esp_lcd_panel_io_tx_param(s_panel_io, cmd->cmd, cmd->data, cmd->data_size);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ST7735 init command 0x%02X failed: %s",
                     cmd->cmd, esp_err_to_name(ret));
            return ret;
        }
        if (cmd->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
    }

    return panel_tx_cmd_u8(LCD_CMD_MADCTL, st7735_initial_madctl());
}

static esp_err_t st7735_set_backlight(bool on)
{
    if (!s_backlight_ready) {
        return ESP_OK;
    }
    return gpio_set_level(MIMI_ST7735_BL_GPIO,
                          on ? st7735_backlight_on_level() : st7735_backlight_off_level());
}

static void apply_screen_state_locked(void)
{
    lv_obj_t *screen = lv_scr_act();
    bool monochrome = backend_is_oled(metrics_backend());
    display_palette_t palette = palette_for_theme(s_theme, monochrome);

    if (!screen) {
        return;
    }

    if (backend_is_color_tft(metrics_backend()) && s_all_on) {
        lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        if (s_container) {
            lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_set_style_bg_color(screen, palette.bg, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        if (s_container) {
            lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_set_style_text_color(screen, palette.text, 0);
    lv_obj_set_style_text_font(screen, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    if (s_container) {
        lv_obj_set_style_bg_color(s_container, palette.bg, 0);
        lv_obj_set_style_bg_opa(s_container, LV_OPA_COVER, 0);
    }
    if (s_content_box) {
        lv_obj_set_style_bg_color(s_content_box, monochrome ? palette.bg : palette.icon_lane, 0);
        lv_obj_set_style_bg_opa(s_content_box, LV_OPA_COVER, 0);
    }
    if (s_sidebar) {
        lv_obj_set_style_bg_color(s_sidebar, monochrome ? palette.bg : palette.surface, 0);
        lv_obj_set_style_bg_opa(s_sidebar, LV_OPA_COVER, 0);
    }
    if (s_status_bar) {
        lv_obj_set_style_bg_color(s_status_bar, monochrome ? palette.bg : palette.accent, 0);
        lv_obj_set_style_bg_opa(s_status_bar, monochrome ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(s_status_bar, monochrome ? 0 : 6, 0);
        lv_obj_set_style_pad_right(s_status_bar, monochrome ? 0 : 6, 0);
    }
    if (s_status_label) {
        lv_obj_set_style_text_color(s_status_label, monochrome ? palette.text : palette.accent_text, 0);
    }
    if (s_emotion_label) {
        lv_obj_set_style_text_color(s_emotion_label, monochrome ? palette.text : lv_color_white(), 0);
    }
    if (s_mute_label) {
        lv_obj_set_style_text_color(s_mute_label, monochrome ? palette.text : palette.accent_text, 0);
    }
    if (s_network_label) {
        lv_obj_set_style_text_color(s_network_label, monochrome ? palette.text : palette.accent_text, 0);
    }
    if (s_battery_label) {
        lv_obj_set_style_text_color(s_battery_label, monochrome ? palette.text : palette.accent_text, 0);
    }
    if (s_message_label) {
        lv_obj_set_style_text_color(s_message_label, palette.text, 0);
    }
}

static void refresh_ui_locked(void)
{
    lv_obj_t *screen = lv_scr_act();

    if (!screen) {
        return;
    }

    build_composed_text();
    apply_screen_state_locked();

    if (s_emotion_label) {
        lv_label_set_text(s_emotion_label, current_main_icon());
    }
    if (s_status_label) {
        lv_label_set_text(s_status_label, s_status_text);
    }
    if (s_message_label) {
        lv_label_set_text(s_message_label, s_composed_text);
    }
    if (s_network_label) {
        lv_label_set_text(s_network_label, "");
    }
    if (s_mute_label) {
        lv_label_set_text(s_mute_label, "");
    }
    if (s_battery_label) {
        lv_label_set_text(s_battery_label, "");
    }

    lv_obj_invalidate(screen);
    if (s_display) {
        lv_refr_now(s_display);
    }
}

static esp_err_t create_ui_locked(void)
{
    int backend = s_requested_backend;
    size_t width = backend_width(backend);
    size_t height = backend_height(backend);
    int icon_lane_width = backend_icon_lane_width(backend);
    int sidebar_width = backend_sidebar_width(backend);
    int status_bar_height = backend_status_bar_height(backend);
    lv_obj_t *screen = lv_scr_act();

    if (!screen || width == 0 || height == 0) {
        return ESP_FAIL;
    }

    lv_obj_clean(screen);
    clear_ui_handles();
    apply_screen_state_locked();

    s_container = lv_obj_create(screen);
    if (!s_container) {
        return ESP_FAIL;
    }
    flat_style(s_container);
    lv_obj_set_size(s_container, (int32_t)width, (int32_t)height);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_container, 0, 0);

    s_content_box = lv_obj_create(s_container);
    if (!s_content_box) {
        return ESP_FAIL;
    }
    flat_style(s_content_box);
    lv_obj_set_size(s_content_box, icon_lane_width, (int32_t)height);

    s_emotion_label = lv_label_create(s_content_box);
    if (!s_emotion_label) {
        return ESP_FAIL;
    }
    lv_obj_set_style_text_font(s_emotion_label, LV_FONT_DEFAULT, 0);
    lv_label_set_text(s_emotion_label, current_main_icon());
    lv_obj_center(s_emotion_label);

    s_sidebar = lv_obj_create(s_container);
    if (!s_sidebar) {
        return ESP_FAIL;
    }
    flat_style(s_sidebar);
    lv_obj_set_size(s_sidebar, sidebar_width, (int32_t)height);
    lv_obj_set_flex_flow(s_sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_sidebar, backend_is_color_tft(backend) ? 4 : 0, 0);

    s_status_bar = lv_obj_create(s_sidebar);
    if (!s_status_bar) {
        return ESP_FAIL;
    }
    flat_style(s_status_bar);
    lv_obj_set_size(s_status_bar, sidebar_width, status_bar_height);
    lv_obj_set_flex_flow(s_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_status_label = lv_label_create(s_status_bar);
    if (!s_status_label) {
        return ESP_FAIL;
    }
    lv_obj_set_width(s_status_label, sidebar_width - DISPLAY_STATUS_ICON_SPACE);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_status_label, LV_FONT_DEFAULT, 0);
    lv_label_set_text(s_status_label, s_status_text);

    s_icons_box = lv_obj_create(s_status_bar);
    if (!s_icons_box) {
        return ESP_FAIL;
    }
    flat_style(s_icons_box);
    lv_obj_set_size(s_icons_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_icons_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_icons_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_icons_box, 2, 0);

    s_mute_label = lv_label_create(s_icons_box);
    s_network_label = lv_label_create(s_icons_box);
    s_battery_label = lv_label_create(s_icons_box);
    if (!s_mute_label || !s_network_label || !s_battery_label) {
        return ESP_FAIL;
    }
    lv_obj_set_style_text_font(s_mute_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_font(s_network_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_font(s_battery_label, LV_FONT_DEFAULT, 0);
    lv_label_set_text(s_mute_label, "");
    lv_label_set_text(s_network_label, "");
    lv_label_set_text(s_battery_label, "");

    s_message_label = lv_label_create(s_sidebar);
    if (!s_message_label) {
        return ESP_FAIL;
    }
    lv_obj_set_style_text_font(s_message_label, LV_FONT_DEFAULT, 0);

    if (backend_is_color_tft(backend)) {
        lv_obj_set_size(s_message_label, sidebar_width - 8,
                        (int32_t)height - status_bar_height - 8);
        lv_obj_set_style_pad_top(s_message_label, 8, 0);
        lv_obj_set_style_text_align(s_message_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_message_label, LV_LABEL_LONG_WRAP);
    } else {
        lv_obj_set_size(s_message_label, sidebar_width, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_left(s_message_label, 2, 0);
        lv_label_set_long_mode(s_message_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    lv_label_set_text(s_message_label, s_composed_text);
    return ESP_OK;
}

static void teardown_display(void)
{
    clear_ui_handles();

    if (backend_is_color_tft(metrics_backend()) && s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, false);
    }
    if (s_backlight_ready) {
        st7735_set_backlight(false);
        s_backlight_ready = false;
    }

    if (s_display) {
        esp_err_t err = lvgl_port_remove_disp(s_display);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "lvgl_port_remove_disp failed: %s", esp_err_to_name(err));
        }
        s_display = NULL;
    }

    if (s_lvgl_initialized) {
        esp_err_t err = lvgl_port_deinit();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "lvgl_port_deinit failed: %s", esp_err_to_name(err));
        }
        s_lvgl_initialized = false;
    }

    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_panel_io) {
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    if (s_spi_bus_initialized) {
        spi_bus_free(MIMI_ST7735_SPI_HOST);
        s_spi_bus_initialized = false;
    }

    s_ready = false;
    s_active_backend = MIMI_DISPLAY_BACKEND_NONE;
}

static esp_err_t prepare_oled_bus(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = MIMI_OLED_I2C_PORT,
        .sda_io_num = MIMI_OLED_SDA_GPIO,
        .scl_io_num = MIMI_OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };

    reset_probe_state();
    s_last_bus_result = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (s_last_bus_result != ESP_OK) {
        return s_last_bus_result;
    }

    s_last_probe_0x3c_result = i2c_master_probe(s_i2c_bus, 0x3C, DISPLAY_PROBE_TIMEOUT_MS);
    s_last_probe_0x3d_result = i2c_master_probe(s_i2c_bus, 0x3D, DISPLAY_PROBE_TIMEOUT_MS);
    s_last_probe_cfg_result = i2c_master_probe(s_i2c_bus, MIMI_OLED_I2C_ADDR, DISPLAY_PROBE_TIMEOUT_MS);
    return s_last_probe_cfg_result;
}

static esp_err_t prepare_st7735_bus(void)
{
    spi_bus_config_t bus_config = {
        .sclk_io_num = MIMI_ST7735_CLK_GPIO,
        .mosi_io_num = MIMI_ST7735_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(MIMI_ST7735_WIDTH * MIMI_ST7735_HEIGHT * sizeof(uint16_t)) + 8,
    };

    reset_probe_state();
    if (!st7735_pins_ready()) {
        ESP_LOGE(TAG,
                 "ST7735 wiring incomplete: MOSI=%d CLK=%d DC=%d CS=%d",
                 MIMI_ST7735_MOSI_GPIO, MIMI_ST7735_CLK_GPIO,
                 MIMI_ST7735_DC_GPIO, MIMI_ST7735_CS_GPIO);
        s_last_bus_result = ESP_ERR_INVALID_ARG;
        return s_last_bus_result;
    }

    s_last_bus_result = spi_bus_initialize(MIMI_ST7735_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (s_last_bus_result == ESP_OK) {
        s_spi_bus_initialized = true;
    }
    return s_last_bus_result;
}

static esp_err_t configure_st7735_backlight(void)
{
    if (!gpio_is_assigned(MIMI_ST7735_BL_GPIO)) {
        return ESP_OK;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << MIMI_ST7735_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        return ret;
    }

    s_backlight_ready = true;
    return st7735_set_backlight(false);
}

static esp_err_t prepare_st7735_cs_line(void)
{
    if (!st7735_cs_conflicts_with_led()) {
        return ESP_OK;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << MIMI_ST7735_CS_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_set_level(MIMI_ST7735_CS_GPIO, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGW(TAG,
             "ST7735 CS shares GPIO%d with onboard LED data, forcing CS low and using SPI without managed CS",
             MIMI_ST7735_CS_GPIO);
    return ESP_OK;
}

static esp_err_t install_oled_panel(void)
{
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = MIMI_OLED_I2C_ADDR,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .scl_speed_hz = MIMI_OLED_I2C_SPEED_HZ,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
    };
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = MIMI_OLED_HEIGHT,
    };
    esp_err_t ret;

    if (s_requested_backend != MIMI_DISPLAY_BACKEND_SSD1306) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    panel_config.vendor_config = &ssd1306_config;

    ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &s_panel_io);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_lcd_new_panel_ssd1306(s_panel_io, &panel_config, &s_panel);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_lcd_panel_reset(s_panel);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_init(s_panel);
    if (ret != ESP_OK) {
        return ret;
    }
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

static esp_err_t install_st7735_panel(void)
{
    int cs_gpio_num = MIMI_ST7735_CS_GPIO;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = MIMI_ST7735_DC_GPIO,
        .cs_gpio_num = cs_gpio_num,
        .pclk_hz = MIMI_ST7735_PCLK_HZ,
        .spi_mode = MIMI_ST7735_SPI_MODE,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = MIMI_ST7735_RST_GPIO,
        .rgb_ele_order = MIMI_ST7735_RGB_ORDER,
        .bits_per_pixel = 16,
    };
    esp_err_t ret;

    ret = configure_st7735_backlight();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = prepare_st7735_cs_line();
    if (ret != ESP_OK) {
        return ret;
    }
    if (st7735_cs_conflicts_with_led()) {
        io_config.cs_gpio_num = -1;
    }

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)MIMI_ST7735_SPI_HOST,
                                   &io_config, &s_panel_io);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_lcd_panel_reset(s_panel);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = st7735_run_init_sequence();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_swap_xy(s_panel, MIMI_ST7735_SWAP_XY);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_mirror(s_panel, MIMI_ST7735_MIRROR_X, MIMI_ST7735_MIRROR_Y);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_set_gap(s_panel, MIMI_ST7735_OFFSET_X, MIMI_ST7735_OFFSET_Y);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_disp_on_off(s_panel, true);
    if (ret != ESP_OK) {
        return ret;
    }
    return st7735_set_backlight(true);
}

static esp_err_t install_lvgl_display(void)
{
    int backend = s_requested_backend;
    bool monochrome = backend_is_oled(backend);
    size_t width = backend_width(backend);
    size_t height = backend_height(backend);

    if (!s_lvgl_initialized) {
        lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        port_cfg.task_priority = 4;
        port_cfg.task_stack = 7168;
        port_cfg.task_max_sleep_ms = 500;
        port_cfg.task_affinity = 0;

        esp_err_t ret = lvgl_port_init(&port_cfg);
        if (ret != ESP_OK) {
            return ret;
        }
        s_lvgl_initialized = true;
    }

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = s_panel_io,
        .panel_handle = s_panel,
        .buffer_size = (uint32_t)(width * height),
        .double_buffer = false,
        .hres = (uint32_t)width,
        .vres = (uint32_t)height,
        .monochrome = monochrome,
        .rotation = {
            .swap_xy = backend_is_color_tft(backend) ? MIMI_ST7735_SWAP_XY : false,
            .mirror_x = backend_is_color_tft(backend) ? MIMI_ST7735_MIRROR_X : MIMI_OLED_MIRROR_X,
            .mirror_y = backend_is_color_tft(backend) ? MIMI_ST7735_MIRROR_Y : MIMI_OLED_MIRROR_Y,
        },
#if LVGL_VERSION_MAJOR >= 9
        .color_format = monochrome ? LV_COLOR_FORMAT_I1 : LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            .buff_dma = backend_is_color_tft(backend),
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = backend_is_color_tft(backend),
#endif
            .full_refresh = true,
        },
    };

    s_display = lvgl_port_add_disp(&display_cfg);
    if (!s_display) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t ensure_ready(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    return display_hal_init();
}

esp_err_t display_hal_init(void)
{
    esp_err_t ret;

#if !MIMI_DISPLAY_ENABLED
    s_last_init_result = ESP_ERR_NOT_SUPPORTED;
    return s_last_init_result;
#endif

    if (s_ready) {
        s_last_init_result = ESP_OK;
        return ESP_OK;
    }

    if (s_requested_backend == MIMI_DISPLAY_BACKEND_NONE) {
        reset_probe_state();
        s_last_init_result = ESP_ERR_NOT_SUPPORTED;
        return s_last_init_result;
    }

    teardown_display();

    if (backend_is_oled(s_requested_backend)) {
        ret = prepare_oled_bus();
    } else if (backend_is_color_tft(s_requested_backend)) {
        ret = prepare_st7735_bus();
    } else {
        ret = ESP_ERR_NOT_SUPPORTED;
    }

    if (ret != ESP_OK) {
        s_last_init_result = ret;
        teardown_display();
        return ret;
    }

    if (!is_supported_backend(s_requested_backend)) {
        s_last_init_result = ESP_ERR_NOT_SUPPORTED;
        teardown_display();
        return s_last_init_result;
    }

    if (backend_is_oled(s_requested_backend)) {
        ret = install_oled_panel();
    } else {
        ret = install_st7735_panel();
    }
    if (ret != ESP_OK) {
        s_last_init_result = ret;
        teardown_display();
        return ret;
    }

    ret = install_lvgl_display();
    if (ret != ESP_OK) {
        s_last_init_result = ret;
        teardown_display();
        return ret;
    }

    ret = display_lock(DISPLAY_LVGL_LOCK_TIMEOUT_MS);
    if (ret != ESP_OK) {
        s_last_init_result = ret;
        teardown_display();
        return ret;
    }

    ret = create_ui_locked();
    display_unlock();
    if (ret != ESP_OK) {
        s_last_init_result = ret;
        teardown_display();
        return ret;
    }

    if (backend_is_oled(s_requested_backend)) {
        ret = panel_tx_cmd_u8(OLED_CMD_SET_CONTRAST, s_contrast);
        if (ret == ESP_OK) {
            ret = panel_tx_cmd(s_inverted ? OLED_CMD_INVERSE_DISPLAY : OLED_CMD_NORMAL_DISPLAY);
        }
        if (ret == ESP_OK) {
            ret = panel_tx_cmd(s_all_on ? OLED_CMD_DISPLAY_ALL_ON : OLED_CMD_DISPLAY_ALL_OFF);
        }
        if (ret == ESP_OK) {
            ret = esp_lcd_panel_disp_on_off(s_panel, s_power_on);
        }
    } else {
        ret = esp_lcd_panel_invert_color(s_panel, s_inverted);
        if (ret == ESP_OK) {
            ret = esp_lcd_panel_disp_on_off(s_panel, s_power_on);
        }
        if (ret == ESP_OK) {
            ret = st7735_set_backlight(s_power_on);
        }
        if (ret == ESP_OK) {
            ret = display_lock(DISPLAY_LVGL_LOCK_TIMEOUT_MS);
            if (ret == ESP_OK) {
                refresh_ui_locked();
                display_unlock();
            }
        }
    }

    if (ret != ESP_OK) {
        s_last_init_result = ret;
        teardown_display();
        return ret;
    }

    s_ready = true;
    s_active_backend = s_requested_backend;
    s_last_init_result = ESP_OK;
    ESP_LOGI(TAG, "Display initialized with backend=%s layout=%s",
             backend_to_name(s_active_backend), layout_name(s_active_backend));
    return ESP_OK;
}

bool display_hal_is_ready(void)
{
    return s_ready;
}

esp_err_t display_hal_set_backend_name(const char *backend_name)
{
    int backend;

    if (!backend_name || !backend_name[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(backend_name, "default") == 0) {
        backend = default_backend();
    } else if (strcmp(backend_name, "ssd1306") == 0) {
        backend = MIMI_DISPLAY_BACKEND_SSD1306;
    } else if (strcmp(backend_name, "sh1106") == 0) {
        backend = MIMI_DISPLAY_BACKEND_SH1106;
    } else if (strcmp(backend_name, "st7735") == 0) {
        backend = MIMI_DISPLAY_BACKEND_ST7735;
    } else if (strcmp(backend_name, "none") == 0) {
        backend = MIMI_DISPLAY_BACKEND_NONE;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    s_requested_backend = backend;
    if (!s_ready) {
        return ESP_OK;
    }
    return display_hal_reinit();
}

esp_err_t display_hal_render(const display_hal_view_t *view)
{
    esp_err_t ret = ensure_ready();

    if (ret != ESP_OK) {
        return ret;
    }
    if (!view) {
        return ESP_ERR_INVALID_ARG;
    }

    copy_full_sanitized(view->status_text, s_status_text, sizeof(s_status_text));
    copy_full_sanitized(view->title_text, s_title_text, sizeof(s_title_text));
    copy_full_sanitized(view->message_text, s_message_text, sizeof(s_message_text));
    copy_full_sanitized(view->icon_name, s_icon_name, sizeof(s_icon_name));
    copy_full_sanitized(view->voice_state, s_voice_state, sizeof(s_voice_state));
    s_theme = view->theme;

    ret = display_lock(DISPLAY_LVGL_LOCK_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    refresh_ui_locked();
    display_unlock();
    return ESP_OK;
}

esp_err_t display_hal_clear(void)
{
    display_hal_view_t view = {
        .status_text = s_status_text,
        .title_text = "",
        .message_text = "",
        .icon_name = "microchip_ai",
        .voice_state = s_voice_state,
        .theme = s_theme,
    };
    return display_hal_render(&view);
}

esp_err_t display_hal_fill(bool on)
{
    return display_hal_set_all_on(on);
}

esp_err_t display_hal_set_invert(bool invert)
{
    esp_err_t ret = ensure_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    s_inverted = invert;
    if (backend_is_oled(metrics_backend())) {
        return panel_tx_cmd(invert ? OLED_CMD_INVERSE_DISPLAY : OLED_CMD_NORMAL_DISPLAY);
    }
    return esp_lcd_panel_invert_color(s_panel, invert);
}

esp_err_t display_hal_set_all_on(bool on)
{
    esp_err_t ret = ensure_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    s_all_on = on;
    if (backend_is_oled(metrics_backend())) {
        return panel_tx_cmd(on ? OLED_CMD_DISPLAY_ALL_ON : OLED_CMD_DISPLAY_ALL_OFF);
    }

    ret = display_lock(DISPLAY_LVGL_LOCK_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    refresh_ui_locked();
    display_unlock();
    return ESP_OK;
}

esp_err_t display_hal_set_power(bool on)
{
    esp_err_t ret = ensure_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    s_power_on = on;

    if (backend_is_color_tft(metrics_backend()) && !on) {
        ret = st7735_set_backlight(false);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    ret = esp_lcd_panel_disp_on_off(s_panel, on);
    if (ret != ESP_OK) {
        return ret;
    }

    if (backend_is_oled(metrics_backend())) {
        if (!on) {
            return ESP_OK;
        }
        ret = panel_tx_cmd_u8(OLED_CMD_SET_CONTRAST, s_contrast);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = panel_tx_cmd(s_inverted ? OLED_CMD_INVERSE_DISPLAY : OLED_CMD_NORMAL_DISPLAY);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = panel_tx_cmd(s_all_on ? OLED_CMD_DISPLAY_ALL_ON : OLED_CMD_DISPLAY_ALL_OFF);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        ret = st7735_set_backlight(on);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (on && !s_all_on) {
        ret = display_lock(DISPLAY_LVGL_LOCK_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        refresh_ui_locked();
        display_unlock();
    }

    return ESP_OK;
}

esp_err_t display_hal_set_contrast(uint8_t contrast)
{
    esp_err_t ret = ensure_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    s_contrast = contrast;
    if (!backend_is_oled(metrics_backend())) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return panel_tx_cmd_u8(OLED_CMD_SET_CONTRAST, contrast);
}

esp_err_t display_hal_reinit(void)
{
    teardown_display();
    return display_hal_init();
}

esp_err_t display_hal_show_text(const char *text)
{
    const char *newline;
    char title_buf[MIMI_DISPLAY_TITLE_LIMIT + 1];
    char message_buf[DISPLAY_MESSAGE_MAX_LEN + 1];
    display_hal_view_t view;

    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }

    newline = strchr(text, '\n');
    if (newline) {
        copy_segment_sanitized(text, (size_t)(newline - text), title_buf, sizeof(title_buf));
        while (*newline == '\n' || *newline == '\r') {
            ++newline;
        }
        copy_full_sanitized(newline, message_buf, sizeof(message_buf));
    } else {
        title_buf[0] = '\0';
        copy_full_sanitized(text, message_buf, sizeof(message_buf));
    }

    memset(&view, 0, sizeof(view));
    view.status_text = s_status_text;
    view.title_text = title_buf;
    view.message_text = message_buf;
    view.icon_name = s_icon_name;
    view.voice_state = s_voice_state;
    view.theme = s_theme;
    return display_hal_render(&view);
}

esp_err_t display_hal_set_emotion(const char *emotion_name)
{
    const char *name = emotion_name;
    const char *icon;
    display_hal_view_t view;

    if (!name || !name[0] || strcmp(name, "default") == 0) {
        name = "microchip_ai";
    }

    icon = font_awesome_get_utf8(name);
    if (!icon || !icon[0]) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(&view, 0, sizeof(view));
    view.status_text = s_status_text;
    view.title_text = s_title_text;
    view.message_text = s_message_text;
    view.icon_name = name;
    view.voice_state = s_voice_state;
    view.theme = s_theme;
    return display_hal_render(&view);
}

esp_err_t display_hal_format_info(char *buf, size_t size)
{
    int backend = metrics_backend();
    const char *theme_name = (s_theme == DISPLAY_HAL_THEME_LIGHT) ? "light" : "dark";

    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (backend == MIMI_DISPLAY_BACKEND_NONE) {
        snprintf(buf, size,
                 "backend=%s ready=%s init=%s layout=%s icon=%s theme=%s voice=%s",
                 backend_to_name(s_requested_backend),
                 s_ready ? "yes" : "no",
                 esp_err_to_name(s_last_init_result),
                 layout_name(backend),
                 s_icon_name,
                 theme_name,
                 s_voice_state);
        return ESP_OK;
    }

    if (backend_is_color_tft(backend)) {
        snprintf(buf, size,
                 "backend=%s ready=%s init=%s width=%u height=%u host=%d mosi=%d clk=%d dc=%d "
                 "cs=%d cs_mode=%s rst=%d bl=%d pclk=%u offset_x=%u offset_y=%u mirror_x=%u "
                 "mirror_y=%u swap_xy=%u layout=%s icon=%s theme=%s voice=%s",
                 backend_to_name(s_requested_backend),
                 s_ready ? "yes" : "no",
                 esp_err_to_name(s_last_init_result),
                 (unsigned)backend_width(backend),
                 (unsigned)backend_height(backend),
                 (int)MIMI_ST7735_SPI_HOST,
                 MIMI_ST7735_MOSI_GPIO,
                 MIMI_ST7735_CLK_GPIO,
                 MIMI_ST7735_DC_GPIO,
                 MIMI_ST7735_CS_GPIO,
                 st7735_cs_conflicts_with_led() ? "forced_low" : "gpio",
                 MIMI_ST7735_RST_GPIO,
                 MIMI_ST7735_BL_GPIO,
                 (unsigned)MIMI_ST7735_PCLK_HZ,
                 (unsigned)MIMI_ST7735_OFFSET_X,
                 (unsigned)MIMI_ST7735_OFFSET_Y,
                 (unsigned)MIMI_ST7735_MIRROR_X,
                 (unsigned)MIMI_ST7735_MIRROR_Y,
                 (unsigned)MIMI_ST7735_SWAP_XY,
                 layout_name(backend),
                 s_icon_name,
                 theme_name,
                 s_voice_state);
    } else {
        snprintf(buf, size,
                 "backend=%s ready=%s init=%s width=%u height=%u sda=%d scl=%d speed=%u addr=0x%02X "
                 "probe_cfg=%s probe_0x3C=%s probe_0x3D=%s layout=%s icon=%s theme=%s voice=%s",
                 backend_to_name(s_requested_backend),
                 s_ready ? "yes" : "no",
                 esp_err_to_name(s_last_init_result),
                 (unsigned)backend_width(backend),
                 (unsigned)backend_height(backend),
                 MIMI_OLED_SDA_GPIO,
                 MIMI_OLED_SCL_GPIO,
                 (unsigned)MIMI_OLED_I2C_SPEED_HZ,
                 (unsigned)MIMI_OLED_I2C_ADDR,
                 esp_err_to_name(s_last_probe_cfg_result),
                 esp_err_to_name(s_last_probe_0x3c_result),
                 esp_err_to_name(s_last_probe_0x3d_result),
                 layout_name(backend),
                 s_icon_name,
                 theme_name,
                 s_voice_state);
    }
    return ESP_OK;
}

const char *display_hal_backend_name(void)
{
    return backend_to_name(s_requested_backend);
}

size_t display_hal_get_width(void)
{
    return backend_width(metrics_backend());
}

size_t display_hal_get_height(void)
{
    return backend_height(metrics_backend());
}

size_t display_hal_get_max_lines(void)
{
    return backend_max_lines(metrics_backend());
}

size_t display_hal_get_max_columns(void)
{
    return backend_max_columns(metrics_backend());
}
