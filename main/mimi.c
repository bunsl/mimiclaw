#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"
#include "channels/feishu/feishu_bot.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "hardware/audio_hal_input.h"
#include "hardware/audio_hal_output.h"
#include "hardware/button_service.h"
#include "hardware/display_state.h"
#include "skills/skill_loader.h"
#include "onboard/wifi_onboard.h"
#include "voice/voice_channel.h"

static const char *TAG = "mimi";

static void log_boot_step(const char *step)
{
    ESP_LOGI(TAG, "BOOT STEP: %s", step);
}

static void on_button_toggle(bool enabled, void *user_ctx)
{
    (void)user_ctx;
    (void)voice_channel_set_continuous_mode(enabled);
}

static void on_button_ptt(bool pressed, void *user_ctx)
{
    (void)user_ctx;
    (void)voice_channel_set_ptt(pressed);
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MIMI_SPIFFS_BASE,
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/* Outbound dispatch task: reads from outbound queue and routes to channels */
static void outbound_dispatch_task(void *arg)
{
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (1) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) continue;

        ESP_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, MIMI_CHAN_FEISHU) == 0) {
            esp_err_t send_err = feishu_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Feishu send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Feishu send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
            esp_err_t ws_err = ws_server_send(msg.chat_id, msg.content);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed for %s: %s", msg.chat_id, esp_err_to_name(ws_err));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_SYSTEM) == 0) {
            ESP_LOGI(TAG, "System message [%s]: %.128s", msg.chat_id, msg.content);
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }
}

void app_main(void)
{
    /* Silence noisy components */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MimiClaw - ESP32-S3 AI Agent");
    ESP_LOGI(TAG, "========================================");

    /* Print memory info */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Phase 1: Core infrastructure */
    log_boot_step("init_nvs.begin");
    ESP_ERROR_CHECK(init_nvs());
    log_boot_step("init_nvs.done");
    log_boot_step("esp_event_loop_create_default.begin");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    log_boot_step("esp_event_loop_create_default.done");
    log_boot_step("init_spiffs.begin");
    ESP_ERROR_CHECK(init_spiffs());
    log_boot_step("init_spiffs.done");

    /* Initialize subsystems */
    log_boot_step("message_bus_init.begin");
    ESP_ERROR_CHECK(message_bus_init());
    log_boot_step("message_bus_init.done");
    log_boot_step("memory_store_init.begin");
    ESP_ERROR_CHECK(memory_store_init());
    log_boot_step("memory_store_init.done");
    log_boot_step("skill_loader_init.begin");
    ESP_ERROR_CHECK(skill_loader_init());
    log_boot_step("skill_loader_init.done");
    log_boot_step("session_mgr_init.begin");
    ESP_ERROR_CHECK(session_mgr_init());
    log_boot_step("session_mgr_init.done");
    log_boot_step("wifi_manager_init.begin");
    ESP_ERROR_CHECK(wifi_manager_init());
    log_boot_step("wifi_manager_init.done");

    /*
     * Initializing LVGL/ST7735 before esp_wifi_init() makes this board reboot in
     * a watchdog loop. Defer the display until the Wi-Fi driver is fully created,
     * then bring the screen online for the rest of boot.
     */
    {
        log_boot_step("display_state_init.begin");
        esp_err_t display_err = display_state_init();
        if (display_err != ESP_OK) {
            ESP_LOGW(TAG, "Display init failed, continuing without screen: %s", esp_err_to_name(display_err));
        }
        log_boot_step("display_state_init.done");
    }

    log_boot_step("http_proxy_init.begin");
    ESP_ERROR_CHECK(http_proxy_init());
    log_boot_step("http_proxy_init.done");
    log_boot_step("feishu_bot_init.begin");
    ESP_ERROR_CHECK(feishu_bot_init());
    log_boot_step("feishu_bot_init.done");
    log_boot_step("llm_proxy_init.begin");
    ESP_ERROR_CHECK(llm_proxy_init());
    log_boot_step("llm_proxy_init.done");
    log_boot_step("tool_registry_init.begin");
    ESP_ERROR_CHECK(tool_registry_init());
    log_boot_step("tool_registry_init.done");
    log_boot_step("cron_service_init.begin");
    ESP_ERROR_CHECK(cron_service_init());
    log_boot_step("cron_service_init.done");
    log_boot_step("heartbeat_init.begin");
    ESP_ERROR_CHECK(heartbeat_init());
    log_boot_step("heartbeat_init.done");
    log_boot_step("agent_loop_init.begin");
    ESP_ERROR_CHECK(agent_loop_init());
    log_boot_step("agent_loop_init.done");

    /* Start Serial CLI first (works without WiFi) */
    log_boot_step("serial_cli_init.begin");
    ESP_ERROR_CHECK(serial_cli_init());
    log_boot_step("serial_cli_init.done");

    {
        log_boot_step("button_service_init.begin");
        esp_err_t button_err = button_service_init();
        if (button_err != ESP_OK) {
            ESP_LOGW(TAG, "Button service unavailable: %s", esp_err_to_name(button_err));
            (void)display_state_show("Button unavailable", esp_err_to_name(button_err),
                                     "triangle_exclamation", "error", 3000);
        } else {
            (void)button_service_register_toggle_cb(on_button_toggle, NULL);
            (void)button_service_register_ptt_cb(on_button_ptt, NULL);
        }
        log_boot_step("button_service_init.done");

        log_boot_step("audio_hal_input_init.begin");
        esp_err_t mic_err = audio_hal_input_init();
        if (mic_err != ESP_OK) {
            ESP_LOGW(TAG, "Microphone input unavailable: %s", esp_err_to_name(mic_err));
            (void)display_state_show("Mic unavailable", esp_err_to_name(mic_err),
                                     "microphone_slash", "error", 3000);
        }
        log_boot_step("audio_hal_input_init.done");

        log_boot_step("audio_hal_output_init.begin");
        esp_err_t speaker_err = audio_hal_output_init();
        if (speaker_err != ESP_OK) {
            ESP_LOGW(TAG, "Speaker output unavailable: %s", esp_err_to_name(speaker_err));
            (void)display_state_show("Speaker unavailable", esp_err_to_name(speaker_err),
                                     "volume_xmark", "error", 3000);
        }
        log_boot_step("audio_hal_output_init.done");
    }

    /* Start WiFi */
    (void)display_state_set_status_key("CONNECTING");
    (void)display_state_set_default_view_key("SCANNING_WIFI", "PLEASE_WAIT", "wifi");
    log_boot_step("wifi_manager_start.begin");
    esp_err_t wifi_err = wifi_manager_start();
    log_boot_step("wifi_manager_start.done");
    bool wifi_ok = false;
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Scanning nearby APs on boot...");
        log_boot_step("wifi_manager_scan_and_print.begin");
        wifi_manager_scan_and_print();
        log_boot_step("wifi_manager_scan_and_print.done");
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        log_boot_step("wifi_manager_wait_connected.begin");
        if (wifi_manager_wait_connected(30000) == ESP_OK) {
            wifi_ok = true;
            ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_get_ip());
            (void)display_state_set_status_key("CONNECTED_TO");
            (void)display_state_set_default_view(NULL, wifi_manager_get_ip(), "wifi");
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout");
            (void)display_state_set_status_key("WARNING");
            (void)display_state_set_default_view("Wi-Fi timeout", "Enter config mode", "wifi_slash");
        }
        log_boot_step("wifi_manager_wait_connected.done");
    } else {
        ESP_LOGW(TAG, "No WiFi credentials configured");
        (void)display_state_set_status_key("WIFI_CONFIG_MODE");
        (void)display_state_set_default_view_key("ENTERING_WIFI_CONFIG_MODE", "PLEASE_WAIT", "wifi_slash");
    }

    if (!wifi_ok) {
        ESP_LOGW(TAG, "Entering WiFi onboarding mode...");
        (void)display_state_set_status_key("WIFI_CONFIG_MODE");
        (void)display_state_set_default_view("MimiClaw-AP", "Open 192.168.4.1", "wifi");
        log_boot_step("wifi_onboard_start.captive.begin");
        wifi_onboard_start(WIFI_ONBOARD_MODE_CAPTIVE);  /* blocks, restarts on success */
        return;  /* unreachable */
    }

    log_boot_step("wifi_onboard_start.admin.begin");
    if (wifi_onboard_start(WIFI_ONBOARD_MODE_ADMIN) != ESP_OK) {
        ESP_LOGW(TAG, "Local admin portal unavailable; continuing without config hotspot");
    }
    log_boot_step("wifi_onboard_start.admin.done");

    {
        log_boot_step("voice_channel_init.begin");
        esp_err_t voice_err = voice_channel_init();
        if (voice_err != ESP_OK) {
            ESP_LOGW(TAG, "Voice channel unavailable: %s", esp_err_to_name(voice_err));
            (void)display_state_show("Voice unavailable", esp_err_to_name(voice_err),
                                     "triangle_exclamation", "error", 3000);
        }
        log_boot_step("voice_channel_init.done");
    }

    {
        /* Outbound dispatch task should start first to avoid dropping early replies. */
        log_boot_step("outbound_dispatch_task.create.begin");
        ESP_ERROR_CHECK((xTaskCreatePinnedToCore(
            outbound_dispatch_task, "outbound",
            MIMI_OUTBOUND_STACK, NULL,
            MIMI_OUTBOUND_PRIO, NULL, MIMI_OUTBOUND_CORE) == pdPASS)
            ? ESP_OK : ESP_FAIL);
        log_boot_step("outbound_dispatch_task.create.done");

        /* Start network-dependent services */
        log_boot_step("agent_loop_start.begin");
        ESP_ERROR_CHECK(agent_loop_start());
        log_boot_step("agent_loop_start.done");
        log_boot_step("feishu_bot_start.begin");
        ESP_ERROR_CHECK(feishu_bot_start());
        log_boot_step("feishu_bot_start.done");
        log_boot_step("cron_service_start.begin");
        cron_service_start();
        log_boot_step("cron_service_start.done");
        log_boot_step("heartbeat_start.begin");
        heartbeat_start();
        log_boot_step("heartbeat_start.done");
        log_boot_step("ws_server_start.begin");
        ESP_ERROR_CHECK(ws_server_start());
        log_boot_step("ws_server_start.done");

        (void)display_state_set_status_text("MimiClaw");
        (void)display_state_set_default_view("READY", "Text channels online", "circle_check");
        ESP_LOGI(TAG, "All services started!");
    }

    ESP_LOGI(TAG, "MimiClaw ready. Type 'help' for CLI commands.");
}
