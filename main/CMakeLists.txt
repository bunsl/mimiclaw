idf_component_register(
    SRCS
        "mimi.c"
        "bus/message_bus.c"
        "wifi/wifi_manager.c"
        "channels/feishu/feishu_bot.c"
        "llm/llm_proxy.c"
        "agent/agent_loop.c"
        "agent/context_builder.c"
        "memory/memory_store.c"
        "memory/session_mgr.c"
        "gateway/ws_server.c"
        "cli/serial_cli.c"
        "proxy/http_proxy.c"
        "cron/cron_service.c"
        "heartbeat/heartbeat.c"
        "hardware/display_hal.c"
        "hardware/display_locale.c"
        "hardware/display_state.c"
        "hardware/button_service.c"
        "hardware/audio_hal_input.c"
        "hardware/audio_hal_output.c"
        "voice/voice_channel.c"
        "tools/tool_registry.c"
        "tools/tool_cron.c"
        "tools/tool_display.c"
        "tools/tool_web_search.c"
        "tools/tool_get_time.c"
        "tools/tool_files.c"
        "tools/tool_gpio.c"
        "tools/tool_led.c"
        "tools/gpio_policy.c"
        "fonts/font_awesome.c"
        "fonts/font_awesome_14_1.c"
        "fonts/font_awesome_30_1.c"
        "fonts/font_noto_basic_14_1.c"
        "skills/skill_loader.c"
        "onboard/wifi_onboard.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_http_client esp_http_server
        esp_https_ota esp_event json spiffs console vfs app_update esp-tls
        esp_timer esp_websocket_client esp_driver_gpio esp_driver_rmt
        esp_lcd esp_driver_i2c esp_driver_spi esp_driver_i2s lvgl esp_lvgl_port
)
