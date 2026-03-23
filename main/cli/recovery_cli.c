#include "cli/recovery_cli.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "channels/feishu/feishu_bot.h"
#include "llm/llm_proxy.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "recovery_cli";
static TaskHandle_t s_recovery_cli_task = NULL;

static void recovery_cli_print_prompt(void)
{
    printf("\r\nmimi> ");
    fflush(stdout);
}

static void recovery_cli_print_help(void)
{
    printf("\r\nAvailable commands:\r\n");
    printf("  help\r\n");
    printf("  set_wifi <ssid> <password>\r\n");
    printf("  set_feishu_creds <app_id> <app_secret>\r\n");
    printf("  set_api_key <key>\r\n");
    printf("  wifi_status\r\n");
    printf("  restart\r\n");
    fflush(stdout);
}

static void recovery_cli_handle_line(char *line)
{
    char *argv[4] = {0};
    int argc = 0;
    char *context = NULL;
    char *token = strtok_r(line, " \t", &context);

    while (token && argc < (int)(sizeof(argv) / sizeof(argv[0]))) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t", &context);
    }

    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], "help") == 0) {
        recovery_cli_print_help();
        return;
    }

    if (strcmp(argv[0], "set_wifi") == 0) {
        if (argc != 3) {
            printf("\r\nUsage: set_wifi <ssid> <password>\r\n");
            fflush(stdout);
            return;
        }

        esp_err_t err = wifi_manager_set_credentials(argv[1], argv[2]);
        if (err == ESP_OK) {
            printf("\r\nWiFi credentials saved for SSID: %s\r\n", argv[1]);
            printf("Run 'restart' to reconnect using the new credentials.\r\n");
        } else {
            printf("\r\nset_wifi failed: %s\r\n", esp_err_to_name(err));
        }
        fflush(stdout);
        return;
    }

    if (strcmp(argv[0], "set_feishu_creds") == 0) {
        if (argc != 3) {
            printf("\r\nUsage: set_feishu_creds <app_id> <app_secret>\r\n");
            fflush(stdout);
            return;
        }

        esp_err_t err = feishu_set_credentials(argv[1], argv[2]);
        if (err == ESP_OK) {
            err = feishu_bot_start();
        }

        if (err == ESP_OK) {
            printf("\r\nFeishu credentials saved. Channel start requested.\r\n");
        } else {
            printf("\r\nset_feishu_creds failed: %s\r\n", esp_err_to_name(err));
        }
        fflush(stdout);
        return;
    }

    if (strcmp(argv[0], "set_api_key") == 0) {
        if (argc != 2) {
            printf("\r\nUsage: set_api_key <key>\r\n");
            fflush(stdout);
            return;
        }

        esp_err_t err = llm_set_api_key(argv[1]);
        if (err == ESP_OK) {
            printf("\r\nAPI key saved.\r\n");
        } else {
            printf("\r\nset_api_key failed: %s\r\n", esp_err_to_name(err));
        }
        fflush(stdout);
        return;
    }

    if (strcmp(argv[0], "wifi_status") == 0) {
        printf("\r\nWiFi connected: %s\r\n", wifi_manager_is_connected() ? "yes" : "no");
        printf("IP: %s\r\n", wifi_manager_get_ip());
        fflush(stdout);
        return;
    }

    if (strcmp(argv[0], "restart") == 0) {
        printf("\r\nRestarting...\r\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        return;
    }

    printf("\r\nUnknown command: %s\r\n", argv[0]);
    printf("Type 'help' for recovery commands.\r\n");
    fflush(stdout);
}

static void recovery_cli_task_main(void *arg)
{
    char line[160];

    (void)arg;

    ESP_LOGI(TAG, "Recovery serial CLI ready. Type 'help' for commands.");

    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        recovery_cli_handle_line(line);
        recovery_cli_print_prompt();
    }
}

esp_err_t recovery_cli_start(void)
{
    if (s_recovery_cli_task != NULL) {
        return ESP_OK;
    }

    if (xTaskCreatePinnedToCore(recovery_cli_task_main,
                                "recovery_cli",
                                4 * 1024,
                                NULL,
                                2,
                                &s_recovery_cli_task,
                                tskNO_AFFINITY) != pdPASS) {
        s_recovery_cli_task = NULL;
        ESP_LOGE(TAG, "Failed to start recovery serial CLI task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void recovery_cli_print_banner(void)
{
    recovery_cli_print_help();
    recovery_cli_print_prompt();
}
