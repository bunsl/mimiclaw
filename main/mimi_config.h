#pragma once

#include "driver/i2s_types.h"
#include "driver/spi_master.h"
#include "esp_lcd_types.h"

/* MimiClaw Global Configuration */

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("mimi_secrets.h")
#include "mimi_secrets.h"
#endif

#ifndef MIMI_SECRET_WIFI_SSID
#define MIMI_SECRET_WIFI_SSID       ""
#endif
#ifndef MIMI_SECRET_WIFI_PASS
#define MIMI_SECRET_WIFI_PASS       ""
#endif
#ifndef MIMI_SECRET_API_KEY
#define MIMI_SECRET_API_KEY         ""
#endif
#ifndef MIMI_SECRET_FEISHU_API_KEY
#define MIMI_SECRET_FEISHU_API_KEY  ""
#endif
#ifndef MIMI_SECRET_WEBSOCKET_API_KEY
#define MIMI_SECRET_WEBSOCKET_API_KEY ""
#endif
#ifndef MIMI_SECRET_CLI_API_KEY
#define MIMI_SECRET_CLI_API_KEY     ""
#endif
#ifndef MIMI_SECRET_SYSTEM_API_KEY
#define MIMI_SECRET_SYSTEM_API_KEY  ""
#endif
#ifndef MIMI_SECRET_MODEL
#define MIMI_SECRET_MODEL           ""
#endif
#ifndef MIMI_SECRET_FEISHU_MODEL
#define MIMI_SECRET_FEISHU_MODEL    ""
#endif
#ifndef MIMI_SECRET_WEBSOCKET_MODEL
#define MIMI_SECRET_WEBSOCKET_MODEL ""
#endif
#ifndef MIMI_SECRET_CLI_MODEL
#define MIMI_SECRET_CLI_MODEL       ""
#endif
#ifndef MIMI_SECRET_SYSTEM_MODEL
#define MIMI_SECRET_SYSTEM_MODEL    ""
#endif
#ifndef MIMI_SECRET_MODEL_PROVIDER
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"
#endif
#ifndef MIMI_SECRET_FEISHU_MODEL_PROVIDER
#define MIMI_SECRET_FEISHU_MODEL_PROVIDER ""
#endif
#ifndef MIMI_SECRET_WEBSOCKET_MODEL_PROVIDER
#define MIMI_SECRET_WEBSOCKET_MODEL_PROVIDER ""
#endif
#ifndef MIMI_SECRET_CLI_MODEL_PROVIDER
#define MIMI_SECRET_CLI_MODEL_PROVIDER ""
#endif
#ifndef MIMI_SECRET_SYSTEM_MODEL_PROVIDER
#define MIMI_SECRET_SYSTEM_MODEL_PROVIDER ""
#endif
#ifndef MIMI_SECRET_PROXY_HOST
#define MIMI_SECRET_PROXY_HOST      ""
#endif
#ifndef MIMI_SECRET_PROXY_PORT
#define MIMI_SECRET_PROXY_PORT      ""
#endif
#ifndef MIMI_SECRET_PROXY_TYPE
#define MIMI_SECRET_PROXY_TYPE      ""
#endif
#ifndef MIMI_SECRET_SEARCH_KEY
#define MIMI_SECRET_SEARCH_KEY      ""
#endif
#ifndef MIMI_SECRET_FEISHU_APP_ID
#define MIMI_SECRET_FEISHU_APP_ID   ""
#endif
#ifndef MIMI_SECRET_FEISHU_APP_SECRET
#define MIMI_SECRET_FEISHU_APP_SECRET ""
#endif
#ifndef MIMI_SECRET_TAVILY_KEY
#define MIMI_SECRET_TAVILY_KEY      ""
#endif
#ifndef MIMI_SECRET_DISPLAY_THEME
#define MIMI_SECRET_DISPLAY_THEME   ""
#endif
#ifndef MIMI_SECRET_DISPLAY_LOCALE
#define MIMI_SECRET_DISPLAY_LOCALE  ""
#endif
#ifndef MIMI_SECRET_VOICE_APPID
#define MIMI_SECRET_VOICE_APPID     ""
#endif
#ifndef MIMI_SECRET_VOICE_ACCESS_TOKEN
#define MIMI_SECRET_VOICE_ACCESS_TOKEN ""
#endif
#ifndef MIMI_SECRET_VOICE_CLUSTER
#define MIMI_SECRET_VOICE_CLUSTER   ""
#endif
#ifndef MIMI_SECRET_VOICE_WS_URL
#define MIMI_SECRET_VOICE_WS_URL    ""
#endif
#ifndef MIMI_SECRET_VOICE_LANGUAGE
#define MIMI_SECRET_VOICE_LANGUAGE  "zh-CN"
#endif
#ifndef MIMI_SECRET_VOICE_CONTINUOUS
#define MIMI_SECRET_VOICE_CONTINUOUS  "0"
#endif

/* Board Profiles */
#define MIMI_BOARD_PROFILE_CLASSIC              0
#define MIMI_BOARD_PROFILE_S3_ST7735_VOICE      1

#ifndef MIMI_BOARD_PROFILE
#if CONFIG_IDF_TARGET_ESP32S3
#define MIMI_BOARD_PROFILE MIMI_BOARD_PROFILE_S3_ST7735_VOICE
#else
#define MIMI_BOARD_PROFILE MIMI_BOARD_PROFILE_CLASSIC
#endif
#endif

/* WiFi */
#define MIMI_WIFI_MAX_RETRY          10
#define MIMI_WIFI_RETRY_BASE_MS      1000
#define MIMI_WIFI_RETRY_MAX_MS       30000

/* Feishu Bot */
#define MIMI_FEISHU_MAX_MSG_LEN          4096
#define MIMI_FEISHU_POLL_STACK           (12 * 1024)
#define MIMI_FEISHU_POLL_PRIO            5
#define MIMI_FEISHU_POLL_CORE            0
#define MIMI_FEISHU_WEBHOOK_PORT         18790
#define MIMI_FEISHU_WEBHOOK_PATH         "/feishu/events"
#define MIMI_FEISHU_WEBHOOK_MAX_BODY     (16 * 1024)

/* Agent Loop */
#define MIMI_AGENT_STACK             (24 * 1024)
#define MIMI_AGENT_PRIO              6
#define MIMI_AGENT_CORE              1
#define MIMI_AGENT_MAX_HISTORY       20
#define MIMI_AGENT_MAX_TOOL_ITER     10
#define MIMI_MAX_TOOL_CALLS          4
#define MIMI_AGENT_SEND_WORKING_STATUS 1

/* Timezone (POSIX TZ format) */
#define MIMI_TIMEZONE                "PST8PDT,M3.2.0,M11.1.0"

/* LLM */
#define MIMI_LLM_DEFAULT_MODEL       "claude-opus-4-5"
#define MIMI_LLM_PROVIDER_DEFAULT    "anthropic"
#define MIMI_LLM_MAX_TOKENS          4096
#define MIMI_LLM_API_URL             "https://api.anthropic.com/v1/messages"
#define MIMI_OPENAI_API_URL          "https://api.openai.com/v1/chat/completions"
#define MIMI_MINIMAX_API_URL         "https://api.minimaxi.com/v1/chat/completions"
#define MIMI_VOLCENGINE_API_URL      "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
#define MIMI_LLM_API_VERSION         "2023-06-01"
#define MIMI_LLM_STREAM_BUF_SIZE     (32 * 1024)
#define MIMI_LLM_LOG_VERBOSE_PAYLOAD 0
#define MIMI_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define MIMI_BUS_QUEUE_LEN           16
#define MIMI_OUTBOUND_STACK          (12 * 1024)
#define MIMI_OUTBOUND_PRIO           5
#define MIMI_OUTBOUND_CORE           0

/* Memory / SPIFFS */
#define MIMI_SPIFFS_BASE             "/spiffs"
#define MIMI_SPIFFS_CONFIG_DIR       MIMI_SPIFFS_BASE "/config"
#define MIMI_SPIFFS_MEMORY_DIR       MIMI_SPIFFS_BASE "/memory"
#define MIMI_SPIFFS_SESSION_DIR      MIMI_SPIFFS_BASE "/sessions"
#define MIMI_MEMORY_FILE             MIMI_SPIFFS_MEMORY_DIR "/MEMORY.md"
#define MIMI_SOUL_FILE               MIMI_SPIFFS_CONFIG_DIR "/SOUL.md"
#define MIMI_USER_FILE               MIMI_SPIFFS_CONFIG_DIR "/USER.md"
#define MIMI_CONTEXT_BUF_SIZE        (16 * 1024)
#define MIMI_SESSION_MAX_MSGS        20

/* Cron / Heartbeat */
#define MIMI_CRON_FILE               MIMI_SPIFFS_BASE "/cron.json"
#define MIMI_CRON_MAX_JOBS           16
#define MIMI_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define MIMI_HEARTBEAT_FILE          MIMI_SPIFFS_BASE "/HEARTBEAT.md"
#define MIMI_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

/* GPIO */
#define MIMI_GPIO_CONFIG_SECTION     1   /* enable GPIO tools */
#define MIMI_LED_DATA_GPIO           48  /* onboard RGB LED data pin (WS2812) */

/* Display */
#define MIMI_DISPLAY_BACKEND_NONE    0
#define MIMI_DISPLAY_BACKEND_SSD1306 1
#define MIMI_DISPLAY_BACKEND_SH1106  2
#define MIMI_DISPLAY_BACKEND_ST7735  3

#define MIMI_DISPLAY_THEME_DARK      0
#define MIMI_DISPLAY_THEME_LIGHT     1

#if MIMI_BOARD_PROFILE == MIMI_BOARD_PROFILE_S3_ST7735_VOICE
#define MIMI_DISPLAY_BACKEND         MIMI_DISPLAY_BACKEND_ST7735
#else
#define MIMI_DISPLAY_BACKEND         MIMI_DISPLAY_BACKEND_NONE
#endif

#define MIMI_DISPLAY_ENABLED         (MIMI_DISPLAY_BACKEND != MIMI_DISPLAY_BACKEND_NONE)
#define MIMI_DIAG_DISABLE_DISPLAY    0
#define MIMI_DISPLAY_DEFAULT_THEME   "dark"
#define MIMI_DISPLAY_DEFAULT_LOCALE  "zh-CN"
#define MIMI_DISPLAY_LOCALE_DIR      MIMI_SPIFFS_BASE "/locales"
#define MIMI_DISPLAY_TEXT_LIMIT      192
#define MIMI_DISPLAY_TITLE_LIMIT     64
#define MIMI_DISPLAY_STATUS_LIMIT    64
#define MIMI_DISPLAY_ICON_LIMIT      32
#define MIMI_DISPLAY_LOCALE_LIMIT    16
#define MIMI_DISPLAY_VOICE_STATE_LIMIT 24

/* Built-in font aliases for LVGL display HAL */
#define BUILTIN_TEXT_FONT            font_noto_basic_14_1
#define BUILTIN_ICON_FONT            font_awesome_14_1
#define BUILTIN_LARGE_ICON_FONT      font_awesome_30_1

/* OLED defaults kept for optional/manual backend override */
#define MIMI_OLED_I2C_PORT           0
#define MIMI_OLED_SDA_GPIO           -1
#define MIMI_OLED_SCL_GPIO           -1
#define MIMI_OLED_I2C_SPEED_HZ       400000
#define MIMI_OLED_I2C_ADDR           0x3C
#define MIMI_OLED_WIDTH              128
#define MIMI_OLED_HEIGHT             32
#define MIMI_OLED_MIRROR_X           0
#define MIMI_OLED_MIRROR_Y           0

/* ST7735 1.44" 128x128 board defaults */
#define MIMI_ST7735_WIDTH            128
#define MIMI_ST7735_HEIGHT           128
#define MIMI_ST7735_SPI_HOST         SPI2_HOST
#define MIMI_ST7735_MOSI_GPIO        41
#define MIMI_ST7735_CLK_GPIO         42
#define MIMI_ST7735_DC_GPIO          47
#define MIMI_ST7735_CS_GPIO          36
#define MIMI_ST7735_RST_GPIO         21
#define MIMI_ST7735_BL_GPIO          45
#define MIMI_ST7735_PCLK_HZ          (20 * 1000 * 1000)
#define MIMI_ST7735_SPI_MODE         0
#define MIMI_ST7735_RGB_ORDER        LCD_RGB_ELEMENT_ORDER_BGR
#define MIMI_ST7735_INVERT_COLOR     1
#define MIMI_ST7735_MIRROR_X         1
#define MIMI_ST7735_MIRROR_Y         1
#define MIMI_ST7735_SWAP_XY          0
#define MIMI_ST7735_OFFSET_X         2
#define MIMI_ST7735_OFFSET_Y         3
#define MIMI_ST7735_BACKLIGHT_INVERT 0

/* Button */
#define MIMI_BUTTON_GPIO             0
#define MIMI_BUTTON_ACTIVE_LEVEL     0
#define MIMI_BUTTON_LONG_PRESS_MS    300

/* Audio Input (INMP441) */
#define MIMI_AUDIO_INPUT_I2S_PORT    I2S_NUM_0
#define MIMI_AUDIO_INPUT_WS_GPIO     4
#define MIMI_AUDIO_INPUT_SCK_GPIO    5
#define MIMI_AUDIO_INPUT_SD_GPIO     6
#define MIMI_AUDIO_INPUT_SAMPLE_RATE 16000
#define MIMI_AUDIO_INPUT_BITS        16
#define MIMI_AUDIO_INPUT_LEVEL_SAMPLES 160

/* Audio Output (MAX98357) */
#define MIMI_AUDIO_OUTPUT_I2S_PORT   I2S_NUM_1
#define MIMI_AUDIO_OUTPUT_BCLK_GPIO  15
#define MIMI_AUDIO_OUTPUT_LRCK_GPIO  16
#define MIMI_AUDIO_OUTPUT_DOUT_GPIO  7
#define MIMI_AUDIO_OUTPUT_SAMPLE_RATE 16000
#define MIMI_AUDIO_OUTPUT_BITS       16

/* Voice */
#define MIMI_VOICE_DEFAULT_LANGUAGE  "zh-CN"
#define MIMI_VOICE_DEFAULT_CLUSTER   ""
#define MIMI_VOICE_DEFAULT_WS_URL    "wss://openspeech.bytedance.com/api/v3/realtime/dialogue"
#define MIMI_VOICE_DEFAULT_CONTINUOUS_MODE 0
#define MIMI_VOICE_DEFAULT_CHAT_ID   "default"
#define MIMI_VOICE_MAX_TEXT          512
#define MIMI_VOICE_WS_BUFFER         (8 * 1024)

/* Skills */
#define MIMI_SKILLS_PREFIX           MIMI_SPIFFS_BASE "/skills/"

/* WebSocket Gateway */
#define MIMI_WS_PORT                 18789
#define MIMI_WS_MAX_CLIENTS          4

/* Serial CLI */
#define MIMI_CLI_STACK               (4 * 1024)
#define MIMI_CLI_PRIO                3
#define MIMI_CLI_CORE                0

/* NVS Namespaces */
#define MIMI_NVS_WIFI                "wifi_config"
#define MIMI_NVS_FEISHU              "feishu_config"
#define MIMI_NVS_LLM                 "llm_config"
#define MIMI_NVS_PROXY               "proxy_config"
#define MIMI_NVS_SEARCH              "search_config"
#define MIMI_NVS_DISPLAY             "display_cfg"
#define MIMI_NVS_VOICE               "voice_cfg"

/* NVS Keys */
#define MIMI_NVS_KEY_SSID            "ssid"
#define MIMI_NVS_KEY_PASS            "password"
#define MIMI_NVS_KEY_FEISHU_APP_ID   "app_id"
#define MIMI_NVS_KEY_FEISHU_APP_SECRET "app_secret"
#define MIMI_NVS_KEY_API_KEY         "api_key"
#define MIMI_NVS_KEY_TAVILY_KEY      "tavily_key"
#define MIMI_NVS_KEY_MODEL           "model"
#define MIMI_NVS_KEY_PROVIDER        "provider"
#define MIMI_NVS_KEY_API_KEY_FEISHU  "key_feishu"
#define MIMI_NVS_KEY_API_KEY_WS      "key_ws"
#define MIMI_NVS_KEY_API_KEY_CLI     "key_cli"
#define MIMI_NVS_KEY_API_KEY_SYSTEM  "key_sys"
#define MIMI_NVS_KEY_MODEL_FEISHU    "model_feishu"
#define MIMI_NVS_KEY_MODEL_WS        "model_ws"
#define MIMI_NVS_KEY_MODEL_CLI       "model_cli"
#define MIMI_NVS_KEY_MODEL_SYSTEM    "model_sys"
#define MIMI_NVS_KEY_PROVIDER_FEISHU "prov_feishu"
#define MIMI_NVS_KEY_PROVIDER_WS     "prov_ws"
#define MIMI_NVS_KEY_PROVIDER_CLI    "prov_cli"
#define MIMI_NVS_KEY_PROVIDER_SYSTEM "prov_sys"
#define MIMI_NVS_KEY_PROXY_HOST      "host"
#define MIMI_NVS_KEY_PROXY_PORT      "port"
#define MIMI_NVS_KEY_PROXY_TYPE      "proxy_type"
#define MIMI_NVS_KEY_DISPLAY_THEME   "theme"
#define MIMI_NVS_KEY_DISPLAY_LOCALE  "locale"
#define MIMI_NVS_KEY_VOICE_APPID     "appid"
#define MIMI_NVS_KEY_VOICE_TOKEN     "token"
#define MIMI_NVS_KEY_VOICE_CLUSTER   "cluster"
#define MIMI_NVS_KEY_VOICE_WS_URL    "ws_url"
#define MIMI_NVS_KEY_VOICE_LANGUAGE  "language"
#define MIMI_NVS_KEY_VOICE_CONT_MODE "cont_mode"

/* WiFi Onboarding (Captive Portal) */
#define MIMI_ONBOARD_AP_PREFIX    "MimiClaw-"
#define MIMI_ONBOARD_AP_PASS      ""          /* open network */
#define MIMI_ONBOARD_HTTP_PORT    80
#define MIMI_ONBOARD_DNS_STACK    (4 * 1024)
#define MIMI_ONBOARD_MAX_SCAN     20
