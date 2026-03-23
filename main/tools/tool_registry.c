#include "tool_registry.h"
#include "mimi_config.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_gpio.h"
#include "tools/tool_led.h"
#include "tools/tool_display.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "tools";

#define MAX_TOOLS 24
#define TOOLS_JSON_PREBUFFER_BYTES (20 * 1024)

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */
static SemaphoreHandle_t s_tools_json_lock = NULL;

static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

static bool append_literal(char **cursor, char *end, const char *text)
{
    size_t len;

    if (!cursor || !*cursor || !end || !text) {
        return false;
    }

    len = strlen(text);
    if ((size_t)(end - *cursor) < len) {
        return false;
    }

    memcpy(*cursor, text, len);
    *cursor += len;
    return true;
}

static bool append_json_string(char **cursor, char *end, const char *text)
{
    const unsigned char *src = (const unsigned char *)(text ? text : "");

    if (!append_literal(cursor, end, "\"")) {
        return false;
    }

    while (*src) {
        char escape_buf[7];

        switch (*src) {
        case '\"':
            if (!append_literal(cursor, end, "\\\"")) {
                return false;
            }
            break;
        case '\\':
            if (!append_literal(cursor, end, "\\\\")) {
                return false;
            }
            break;
        case '\b':
            if (!append_literal(cursor, end, "\\b")) {
                return false;
            }
            break;
        case '\f':
            if (!append_literal(cursor, end, "\\f")) {
                return false;
            }
            break;
        case '\n':
            if (!append_literal(cursor, end, "\\n")) {
                return false;
            }
            break;
        case '\r':
            if (!append_literal(cursor, end, "\\r")) {
                return false;
            }
            break;
        case '\t':
            if (!append_literal(cursor, end, "\\t")) {
                return false;
            }
            break;
        default:
            if (*src < 0x20) {
                snprintf(escape_buf, sizeof(escape_buf), "\\u%04x", *src);
                if (!append_literal(cursor, end, escape_buf)) {
                    return false;
                }
            } else {
                if ((size_t)(end - *cursor) < 1) {
                    return false;
                }
                **cursor = (char)*src;
                (*cursor)++;
            }
            break;
        }
        ++src;
    }

    return append_literal(cursor, end, "\"");
}

static void build_tools_json(void)
{
    char *new_tools_json;
    char *cursor;
    char *end;
    bool ok = true;

    ESP_LOGI(TAG, "build_tools_json: begin tool_count=%d", s_tool_count);

    new_tools_json = malloc(TOOLS_JSON_PREBUFFER_BYTES);
    if (!new_tools_json) {
        ESP_LOGE(TAG, "Failed to allocate tools JSON buffer");
        return;
    }

    cursor = new_tools_json;
    end = new_tools_json + TOOLS_JSON_PREBUFFER_BYTES - 1;

    ok = append_literal(&cursor, end, "[");
    for (int i = 0; ok && i < s_tool_count; i++) {
        if (i > 0) {
            ok = append_literal(&cursor, end, ",");
        }
        if (!ok) {
            break;
        }

        ok = append_literal(&cursor, end, "{\"name\":");
        ok = ok && append_json_string(&cursor, end, s_tools[i].name);
        ok = ok && append_literal(&cursor, end, ",\"description\":");
        ok = ok && append_json_string(&cursor, end, s_tools[i].description);
        ok = ok && append_literal(&cursor, end, ",\"input_schema\":");
        ok = ok && append_literal(&cursor, end,
                                   s_tools[i].input_schema_json ? s_tools[i].input_schema_json : "null");
        ok = ok && append_literal(&cursor, end, "}");

        if ((i & 0x3) == 0x3) {
            vTaskDelay(1);
        }
    }

    if (ok) {
        ok = append_literal(&cursor, end, "]");
    }
    if (!ok) {
        ESP_LOGE(TAG, "Tools JSON buffer overflow");
        free(new_tools_json);
        return;
    }

    *cursor = '\0';
    free(s_tools_json);
    s_tools_json = new_tools_json;
    ESP_LOGI(TAG, "build_tools_json: print complete len=%d", (int)strlen(s_tools_json));

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;
    if (!s_tools_json_lock) {
        s_tools_json_lock = xSemaphoreCreateMutex();
        if (!s_tools_json_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    free(s_tools_json);
    s_tools_json = NULL;

    /* Register web_search */
    tool_web_search_init();

    mimi_tool_t ws = {
        .name = "web_search",
        .description = "Search the web for current information via Tavily (preferred) or Brave when configured.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
            "\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    };
    register_tool(&ws);

    /* Register get_current_time */
    mimi_tool_t gt = {
        .name = "get_current_time",
        .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_time_execute,
    };
    register_tool(&gt);

    /* Register read_file */
    mimi_tool_t rf = {
        .name = "read_file",
        .description = "Read a file from SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_read_file_execute,
    };
    register_tool(&rf);

    /* Register write_file */
    mimi_tool_t wf = {
        .name = "write_file",
        .description = "Write or overwrite a file on SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file_execute,
    };
    register_tool(&wf);

    /* Register edit_file */
    mimi_tool_t ef = {
        .name = "edit_file",
        .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
            "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
            "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .execute = tool_edit_file_execute,
    };
    register_tool(&ef);

    /* Register list_dir */
    mimi_tool_t ld = {
        .name = "list_dir",
        .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. " MIMI_SPIFFS_BASE "/memory/\"}},"
            "\"required\":[]}",
        .execute = tool_list_dir_execute,
    };
    register_tool(&ld);

    /* Register cron_add */
    mimi_tool_t ca = {
        .name = "cron_add",
        .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
            "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
            "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
            "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
            "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'feishu' or 'websocket'). If omitted, current turn channel is used when available\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Required for non-system channels. If omitted during a user turn, current chat_id is used\"}"
            "},"
            "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
        .execute = tool_cron_add_execute,
    };
    register_tool(&ca);

    /* Register cron_list */
    mimi_tool_t cl = {
        .name = "cron_list",
        .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_cron_list_execute,
    };
    register_tool(&cl);

    /* Register cron_remove */
    mimi_tool_t cr = {
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by its ID.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_cron_remove_execute,
    };
    register_tool(&cr);

    /* Register GPIO tools */
    tool_gpio_init();

    mimi_tool_t gw = {
        .name = "gpio_write",
        .description = "Set a GPIO pin HIGH or LOW. Controls LEDs, relays, and other digital outputs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number\"},"
            "\"state\":{\"type\":\"integer\",\"description\":\"1 for HIGH, 0 for LOW\"}},"
            "\"required\":[\"pin\",\"state\"]}",
        .execute = tool_gpio_write_execute,
    };
    register_tool(&gw);

    mimi_tool_t gr = {
        .name = "gpio_read",
        .description = "Read a GPIO pin state. Returns HIGH or LOW. Use for checking switches, sensors, and digital inputs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number\"}},"
            "\"required\":[\"pin\"]}",
        .execute = tool_gpio_read_execute,
    };
    register_tool(&gr);

    mimi_tool_t ga = {
        .name = "gpio_read_all",
        .description = "Read all allowed GPIO pin states in a single call. Returns each pin's HIGH/LOW state.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_gpio_read_all_execute,
    };
    register_tool(&ga);

    /* Register onboard RGB LED tool */
    tool_led_init();

    mimi_tool_t ls = {
        .name = "led_set",
        .description = "Set onboard RGB LED color using WS2812 data protocol. Use this for board RGB status LED.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"r\":{\"type\":\"integer\",\"description\":\"Red 0-255\"},"
            "\"g\":{\"type\":\"integer\",\"description\":\"Green 0-255\"},"
            "\"b\":{\"type\":\"integer\",\"description\":\"Blue 0-255\"}},"
            "\"required\":[\"r\",\"g\",\"b\"]}",
        .execute = tool_led_set_execute,
    };
    register_tool(&ls);

    /* Register display tools */
    mimi_tool_t ds = {
        .name = "display_show_text",
        .description = "Show a short message on the onboard display with optional title, icon, role, theme-aware layout, and optional timeout.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"title\":{\"type\":\"string\",\"description\":\"Optional short title shown above the main text\"},"
            "\"text\":{\"type\":\"string\",\"description\":\"Main text to show on the display\"},"
            "\"icon\":{\"type\":\"string\",\"description\":\"Optional Font Awesome icon name such as wifi, microphone, triangle_exclamation, or microchip_ai\"},"
            "\"role\":{\"type\":\"string\",\"description\":\"Optional semantic role used to pick a default icon (assistant|user|system|tool|error)\"},"
            "\"duration_ms\":{\"type\":\"integer\",\"description\":\"Optional timeout in milliseconds after which the previous screen content is restored\"}"
            "},"
            "\"required\":[\"text\"]}",
        .execute = tool_display_show_text_execute,
    };
    register_tool(&ds);

    mimi_tool_t dc = {
        .name = "display_clear",
        .description = "Clear the current display message area while keeping the board status bar active.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_display_clear_execute,
    };
    register_tool(&dc);

    mimi_tool_t dt = {
        .name = "display_set_theme",
        .description = "Set the onboard display theme and persist it across restarts. Supported values: dark, light.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"theme\":{\"type\":\"string\",\"description\":\"Theme name: dark or light\"}},"
            "\"required\":[\"theme\"]}",
        .execute = tool_display_set_theme_execute,
    };
    register_tool(&dt);

    mimi_tool_t dl = {
        .name = "display_set_locale",
        .description = "Set the onboard display locale and persist it across restarts. Example values: zh-CN, en-US, ja-JP.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"locale\":{\"type\":\"string\",\"description\":\"Locale code such as zh-CN, en-US, or ja-JP\"}},"
            "\"required\":[\"locale\"]}",
        .execute = tool_display_set_locale_execute,
    };
    register_tool(&dl);

    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

const char *tool_registry_get_tools_json(void)
{
    if (s_tools_json) {
        return s_tools_json;
    }
    if (!s_tools_json_lock) {
        return NULL;
    }
    if (xSemaphoreTake(s_tools_json_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return s_tools_json;
    }
    if (!s_tools_json) {
        build_tools_json();
    }
    xSemaphoreGive(s_tools_json_lock);
    return s_tools_json;
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    ESP_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return ESP_ERR_NOT_FOUND;
}
