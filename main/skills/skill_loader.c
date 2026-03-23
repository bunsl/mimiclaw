#include "skills/skill_loader.h"
#include "mimi_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "skills";
static const char *SKILLS_DIR = MIMI_SPIFFS_BASE "/skills";

/*
 * Boot-time SPIFFS lookups became too expensive once locale assets were added.
 * SPIFFS is a flat filesystem, so even opening a single named file can degrade
 * into a long linear lookup across the whole object table. That is acceptable
 * for on-demand reads, but not on the main boot path.
 *
 * Keep the manifest in firmware for fast boot, while still reading the markdown
 * bodies from SPIFFS on demand so the skill content remains in the same place.
 */
static const char *const s_skill_manifest[] = {
    "daily-briefing.md",
    "display-control.md",
    "gpio-control.md",
    "led-control.md",
    "skill-creator.md",
    "weather.md",
};

static size_t skill_manifest_count(void)
{
    return sizeof(s_skill_manifest) / sizeof(s_skill_manifest[0]);
}

esp_err_t skill_loader_init(void)
{
    ESP_LOGI(TAG, "Initializing skills system");
    ESP_LOGI(TAG, "Skills system ready (%d skills from built-in manifest)",
             (int)skill_manifest_count());
    return ESP_OK;
}

/* Build skills summary for system prompt */

static void extract_title(const char *line, size_t len, char *out, size_t out_size)
{
    const char *start = line;
    size_t copy;

    if (len >= 2 && line[0] == '#' && line[1] == ' ') {
        start = line + 2;
        len -= 2;
    }

    while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r' || start[len - 1] == ' ')) {
        len--;
    }

    copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, start, copy);
    out[copy] = '\0';
}

static void extract_description(FILE *f, char *out, size_t out_size)
{
    size_t off = 0;
    char line[256];

    while (fgets(line, sizeof(line), f) && off < out_size - 1) {
        size_t len = strlen(line);
        size_t copy;

        if (len == 0 || (len == 1 && line[0] == '\n') ||
            (len >= 2 && line[0] == '#' && line[1] == '#')) {
            break;
        }

        if (off == 0 && line[0] == '\n') {
            continue;
        }

        if (line[len - 1] == '\n') {
            line[len - 1] = ' ';
        }

        copy = len < out_size - off - 1 ? len : out_size - off - 1;
        memcpy(out + off, line, copy);
        off += copy;
    }

    while (off > 0 && out[off - 1] == ' ') {
        off--;
    }
    out[off] = '\0';
}

size_t skill_loader_build_summary(char *buf, size_t size)
{
    size_t off = 0;
    size_t i;

    if (!buf || size == 0) {
        return 0;
    }

    buf[0] = '\0';

    for (i = 0; i < skill_manifest_count() && off < size - 1; ++i) {
        const char *skill_name = s_skill_manifest[i];
        char full_path[296];
        FILE *f;
        char first_line[128];
        char title[64];
        char desc[256];

        snprintf(full_path, sizeof(full_path), "%s/%s", SKILLS_DIR, skill_name);

        f = fopen(full_path, "r");
        if (!f) {
            ESP_LOGW(TAG, "Cannot open skill file: %s", full_path);
            vTaskDelay(1);
            continue;
        }

        if (!fgets(first_line, sizeof(first_line), f)) {
            fclose(f);
            vTaskDelay(1);
            continue;
        }

        extract_title(first_line, strlen(first_line), title, sizeof(title));
        extract_description(f, desc, sizeof(desc));
        fclose(f);

        off += snprintf(buf + off, size - off,
                        "- **%s**: %s (read with: read_file %s)\n",
                        title, desc, full_path);

        /*
         * These SPIFFS opens still walk a flat object table, so yield between
         * files to keep the rest of the system responsive while a summary is
         * assembled on demand.
         */
        vTaskDelay(1);
    }

    buf[off] = '\0';
    ESP_LOGI(TAG, "Skills summary: %d bytes", (int)off);
    return off;
}
