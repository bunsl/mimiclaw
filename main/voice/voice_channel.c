#include "voice/voice_channel.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "bus/message_bus.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware/audio_hal_input.h"
#include "hardware/audio_hal_output.h"
#include "hardware/button_service.h"
#include "hardware/display_state.h"
#include "memory/session_mgr.h"
#include "mimi_config.h"
#include "nvs.h"

static const char *TAG = "voice_channel";

#define VOICE_EVENT_WS_CONNECTED   BIT0
#define VOICE_EVENT_SESSION_READY  BIT1
#define VOICE_EVENT_ERROR          BIT2

#define VOICE_WS_PROTO_VERSION     0x01
#define VOICE_WS_HEADER_SIZE       0x01

#define VOICE_MSG_FULL_CLIENT_REQ  0x01
#define VOICE_MSG_AUDIO_ONLY_REQ   0x02
#define VOICE_MSG_FULL_SERVER_RESP 0x09
#define VOICE_MSG_SERVER_ACK       0x0B
#define VOICE_MSG_FRONTEND_RESP    0x0C
#define VOICE_MSG_SERVER_ERROR     0x0F

#define VOICE_FLAG_NO_SEQUENCE     0x00
#define VOICE_FLAG_HAS_SEQUENCE    0x01
#define VOICE_FLAG_LAST_PACKAGE    0x02
#define VOICE_FLAG_NEG_SEQUENCE    (VOICE_FLAG_HAS_SEQUENCE | VOICE_FLAG_LAST_PACKAGE)

#define VOICE_SERIAL_NONE          0x00
#define VOICE_SERIAL_JSON          0x01

#define VOICE_COMPRESS_NONE        0x00

#define VOICE_RESOURCE_ID          "volc.speech.dialog"
#define VOICE_COMPAT_APP_KEY       "PlgvMymc7f3tQnJ6"
#define VOICE_DEFAULT_BOT_NAME     "Doubao"

#define VOICE_AUDIO_READ_SAMPLES   320
#define VOICE_WS_HEADERS_MAX       768

typedef struct {
    char appid[96];
    char access_token[256];
    char cluster[64];
    char ws_url[256];
    char language[24];
    bool continuous_mode_default;
} voice_config_t;

typedef struct {
    uint8_t message_type;
    uint8_t flags;
    uint8_t serialization;
    uint8_t compression;
    bool last_package;
    int outer_sequence;
    int ack_sequence;
    int error_code;
    uint32_t payload_size;
    const uint8_t *payload;
    size_t payload_len;
} voice_frame_t;

static SemaphoreHandle_t s_lock = NULL;
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_task = NULL;
static esp_websocket_client_handle_t s_ws_client = NULL;
static voice_config_t s_cfg = {0};

static bool s_initialized = false;
static bool s_ws_connected = false;
static bool s_session_ready = false;
static bool s_capture_requested = false;
static bool s_ptt_active = false;
static bool s_continuous_mode = false;
static bool s_start_session_requested = false;
static bool s_end_of_input_requested = false;
static bool s_connect_test_requested = false;
static bool s_transport_faulted = false;
static uint32_t s_next_connect_tick = 0;
static int s_next_sequence = 1;
static int s_remote_sample_rate = MIMI_AUDIO_OUTPUT_SAMPLE_RATE;

static char s_state[24] = "idle";
static char s_connect_id[48] = {0};
static char s_session_id[96] = MIMI_VOICE_DEFAULT_CHAT_ID;
static char s_last_error[160] = {0};
static char s_last_notice[160] = {0};
static char s_partial_user_text[MIMI_VOICE_MAX_TEXT] = {0};
static char s_partial_assistant_text[MIMI_VOICE_MAX_TEXT] = {0};

static uint8_t *s_rx_buf = NULL;
static size_t s_rx_cap = 0;

static void voice_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static bool voice_contains_nocase(const char *text, const char *needle)
{
    size_t nlen;

    if (!text || !needle || !needle[0]) {
        return false;
    }

    nlen = strlen(needle);
    for (const char *p = text; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i]) {
            char a = p[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            ++i;
        }
        if (i == nlen) {
            return true;
        }
    }
    return false;
}

static bool voice_parse_bool_string(const char *value)
{
    if (!value || !value[0]) {
        return false;
    }
    return strcmp(value, "1") == 0 ||
           voice_contains_nocase(value, "true") ||
           voice_contains_nocase(value, "on") ||
           voice_contains_nocase(value, "yes");
}

static void voice_set_notice_locked(const char *notice)
{
    voice_copy_text(s_last_notice, sizeof(s_last_notice), notice);
}

static void voice_set_error_locked(const char *error)
{
    voice_copy_text(s_last_error, sizeof(s_last_error), error);
    xEventGroupSetBits(s_events, VOICE_EVENT_ERROR);
}

static void voice_clear_error_locked(void)
{
    s_last_error[0] = '\0';
    xEventGroupClearBits(s_events, VOICE_EVENT_ERROR);
}

static void voice_generate_connect_id_locked(void)
{
    uint32_t a = esp_random();
    uint32_t b = esp_random();
    snprintf(s_connect_id, sizeof(s_connect_id), "%08lx%08lx",
             (unsigned long)a, (unsigned long)b);
}

static void voice_set_display_state(const char *state, const char *title,
                                    const char *text, const char *icon)
{
    (void)display_state_set_voice_state(state);
    if (title || text || icon) {
        (void)display_state_set_default_view(title, text, icon);
    }
}

static void voice_set_state_locked(const char *state, const char *title,
                                   const char *text, const char *icon)
{
    voice_copy_text(s_state, sizeof(s_state), state ? state : "idle");
    voice_set_display_state(s_state, title, text, icon);
}

static const char *voice_chat_id_locked(void)
{
    return s_session_id[0] ? s_session_id : MIMI_VOICE_DEFAULT_CHAT_ID;
}

static void voice_session_append_locked(const char *role, const char *content)
{
    if (!role || !content || !content[0]) {
        return;
    }
    if (session_append(MIMI_CHAN_VOICE, voice_chat_id_locked(), role, content) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to append voice session role=%s", role);
    }
}

static void voice_reset_buffers_locked(void)
{
    s_partial_user_text[0] = '\0';
    s_partial_assistant_text[0] = '\0';
    s_next_sequence = 1;
    s_remote_sample_rate = MIMI_AUDIO_OUTPUT_SAMPLE_RATE;
}

static void voice_maybe_update_sample_rate_locked(cJSON *root)
{
    const char *keys[] = {"sample_rate", "rate"};
    cJSON *stack[32];
    int top = 0;

    if (!root) {
        return;
    }

    stack[top++] = root;
    while (top > 0) {
        cJSON *node = stack[--top];
        if (cJSON_IsObject(node)) {
            for (cJSON *child = node->child; child; child = child->next) {
                for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
                    if (child->string && strcmp(child->string, keys[i]) == 0 && cJSON_IsNumber(child)) {
                        int rate = child->valueint;
                        if (rate >= 8000 && rate <= 48000 && rate != s_remote_sample_rate) {
                            s_remote_sample_rate = rate;
                            (void)audio_hal_output_set_sample_rate(rate);
                            ESP_LOGI(TAG, "Remote audio sample rate -> %dHz", rate);
                        }
                        return;
                    }
                }
                if ((cJSON_IsObject(child) || cJSON_IsArray(child)) &&
                    top < (int)(sizeof(stack) / sizeof(stack[0]))) {
                    stack[top++] = child;
                }
            }
        } else if (cJSON_IsArray(node)) {
            for (cJSON *child = node->child; child; child = child->next) {
                if ((cJSON_IsObject(child) || cJSON_IsArray(child)) &&
                    top < (int)(sizeof(stack) / sizeof(stack[0]))) {
                    stack[top++] = child;
                }
            }
        }
    }
}

static const char *voice_find_text_in_object_keys(cJSON *root, const char *const *object_keys,
                                                  size_t object_key_count)
{
    cJSON *stack[48];
    int top = 0;

    if (!root) {
        return NULL;
    }

    stack[top++] = root;
    while (top > 0) {
        cJSON *node = stack[--top];
        if (!cJSON_IsObject(node) && !cJSON_IsArray(node)) {
            continue;
        }

        for (cJSON *child = node->child; child; child = child->next) {
            if (cJSON_IsObject(child) && child->string) {
                bool matched = false;
                for (size_t i = 0; i < object_key_count; ++i) {
                    if (voice_contains_nocase(child->string, object_keys[i])) {
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    cJSON *text = cJSON_GetObjectItemCaseSensitive(child, "text");
                    if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) {
                        return text->valuestring;
                    }
                    text = cJSON_GetObjectItemCaseSensitive(child, "content");
                    if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) {
                        return text->valuestring;
                    }
                    text = cJSON_GetObjectItemCaseSensitive(child, "message");
                    if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) {
                        return text->valuestring;
                    }
                }
            }
            if ((cJSON_IsObject(child) || cJSON_IsArray(child)) &&
                top < (int)(sizeof(stack) / sizeof(stack[0]))) {
                stack[top++] = child;
            }
        }
    }

    return NULL;
}

static const char *voice_find_text_by_value_keys(cJSON *root, const char *const *keys, size_t key_count)
{
    cJSON *stack[48];
    int top = 0;

    if (!root) {
        return NULL;
    }

    stack[top++] = root;
    while (top > 0) {
        cJSON *node = stack[--top];
        if (!cJSON_IsObject(node) && !cJSON_IsArray(node)) {
            continue;
        }

        for (cJSON *child = node->child; child; child = child->next) {
            if (child->string && cJSON_IsString(child) && child->valuestring && child->valuestring[0]) {
                for (size_t i = 0; i < key_count; ++i) {
                    if (voice_contains_nocase(child->string, keys[i])) {
                        return child->valuestring;
                    }
                }
            }
            if ((cJSON_IsObject(child) || cJSON_IsArray(child)) &&
                top < (int)(sizeof(stack) / sizeof(stack[0]))) {
                stack[top++] = child;
            }
        }
    }

    return NULL;
}

static bool voice_json_flag_true(cJSON *root, const char *const *keys, size_t key_count)
{
    cJSON *stack[32];
    int top = 0;

    if (!root) {
        return false;
    }

    stack[top++] = root;
    while (top > 0) {
        cJSON *node = stack[--top];
        if (!cJSON_IsObject(node) && !cJSON_IsArray(node)) {
            continue;
        }

        for (cJSON *child = node->child; child; child = child->next) {
            if (child->string) {
                for (size_t i = 0; i < key_count; ++i) {
                    if (voice_contains_nocase(child->string, keys[i])) {
                        if (cJSON_IsBool(child)) {
                            return cJSON_IsTrue(child);
                        }
                        if (cJSON_IsNumber(child)) {
                            return child->valueint != 0;
                        }
                        if (cJSON_IsString(child)) {
                            return voice_parse_bool_string(child->valuestring);
                        }
                    }
                }
            }
            if ((cJSON_IsObject(child) || cJSON_IsArray(child)) &&
                top < (int)(sizeof(stack) / sizeof(stack[0]))) {
                stack[top++] = child;
            }
        }
    }

    return false;
}

static const char *voice_extract_user_text(cJSON *root)
{
    const char *user_objects[] = {"asr", "speech", "transcript", "result", "query", "user"};
    const char *user_keys[] = {"transcript", "question", "query", "heard"};
    const char *text = voice_find_text_in_object_keys(root, user_objects,
                                                      sizeof(user_objects) / sizeof(user_objects[0]));
    if (text) {
        return text;
    }
    return voice_find_text_by_value_keys(root, user_keys, sizeof(user_keys) / sizeof(user_keys[0]));
}

static const char *voice_extract_assistant_text(cJSON *root)
{
    const char *assistant_objects[] = {"chat", "reply", "answer", "assistant", "response", "tts"};
    const char *assistant_keys[] = {"reply", "answer", "response", "assistant", "content"};
    const char *text = voice_find_text_in_object_keys(root, assistant_objects,
                                                      sizeof(assistant_objects) / sizeof(assistant_objects[0]));
    if (text) {
        return text;
    }
    return voice_find_text_by_value_keys(root, assistant_keys, sizeof(assistant_keys) / sizeof(assistant_keys[0]));
}

static void voice_note_user_text_locked(const char *text, bool final_text)
{
    if (!text || !text[0]) {
        return;
    }
    if (strncmp(s_partial_user_text, text, sizeof(s_partial_user_text) - 1) == 0 && !final_text) {
        return;
    }

    voice_copy_text(s_partial_user_text, sizeof(s_partial_user_text), text);
    voice_set_state_locked(s_capture_requested ? (s_ptt_active ? "listening_ptt" : "listening_toggle") : "thinking",
                           "Heard", s_partial_user_text, "microphone");
    if (final_text) {
        voice_session_append_locked("user", s_partial_user_text);
    }
}

static void voice_note_assistant_text_locked(const char *text, bool final_text)
{
    if (!text || !text[0]) {
        return;
    }
    if (strncmp(s_partial_assistant_text, text, sizeof(s_partial_assistant_text) - 1) == 0 && !final_text) {
        return;
    }

    voice_copy_text(s_partial_assistant_text, sizeof(s_partial_assistant_text), text);
    voice_set_state_locked("speaking", "Reply", s_partial_assistant_text, "comments");
    if (final_text) {
        voice_session_append_locked("assistant", s_partial_assistant_text);
    }
}

static void voice_load_config_locked(void)
{
    nvs_handle_t nvs;
    char temp[256];
    size_t len;

    memset(&s_cfg, 0, sizeof(s_cfg));
    voice_copy_text(s_cfg.appid, sizeof(s_cfg.appid), MIMI_SECRET_VOICE_APPID);
    voice_copy_text(s_cfg.access_token, sizeof(s_cfg.access_token), MIMI_SECRET_VOICE_ACCESS_TOKEN);
    voice_copy_text(s_cfg.cluster, sizeof(s_cfg.cluster), MIMI_SECRET_VOICE_CLUSTER);
    voice_copy_text(s_cfg.ws_url, sizeof(s_cfg.ws_url),
                    MIMI_SECRET_VOICE_WS_URL[0] ? MIMI_SECRET_VOICE_WS_URL : MIMI_VOICE_DEFAULT_WS_URL);
    voice_copy_text(s_cfg.language, sizeof(s_cfg.language),
                    MIMI_SECRET_VOICE_LANGUAGE[0] ? MIMI_SECRET_VOICE_LANGUAGE : MIMI_VOICE_DEFAULT_LANGUAGE);
    s_cfg.continuous_mode_default = voice_parse_bool_string(MIMI_SECRET_VOICE_CONTINUOUS);

    if (nvs_open(MIMI_NVS_VOICE, NVS_READONLY, &nvs) == ESP_OK) {
        len = sizeof(temp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_VOICE_APPID, temp, &len) == ESP_OK && temp[0]) {
            voice_copy_text(s_cfg.appid, sizeof(s_cfg.appid), temp);
        }
        len = sizeof(temp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_VOICE_TOKEN, temp, &len) == ESP_OK && temp[0]) {
            voice_copy_text(s_cfg.access_token, sizeof(s_cfg.access_token), temp);
        }
        len = sizeof(temp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_VOICE_CLUSTER, temp, &len) == ESP_OK && temp[0]) {
            voice_copy_text(s_cfg.cluster, sizeof(s_cfg.cluster), temp);
        }
        len = sizeof(temp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_VOICE_WS_URL, temp, &len) == ESP_OK && temp[0]) {
            voice_copy_text(s_cfg.ws_url, sizeof(s_cfg.ws_url), temp);
        }
        len = sizeof(temp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_VOICE_LANGUAGE, temp, &len) == ESP_OK && temp[0]) {
            voice_copy_text(s_cfg.language, sizeof(s_cfg.language), temp);
        }
        len = sizeof(temp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_VOICE_CONT_MODE, temp, &len) == ESP_OK && temp[0]) {
            s_cfg.continuous_mode_default = voice_parse_bool_string(temp);
        }
        nvs_close(nvs);
    }

    if (!s_cfg.ws_url[0]) {
        voice_copy_text(s_cfg.ws_url, sizeof(s_cfg.ws_url), MIMI_VOICE_DEFAULT_WS_URL);
    }
    if (!s_cfg.language[0]) {
        voice_copy_text(s_cfg.language, sizeof(s_cfg.language), MIMI_VOICE_DEFAULT_LANGUAGE);
    }
}

static bool voice_has_credentials_locked(void)
{
    return s_cfg.appid[0] && s_cfg.access_token[0] && s_cfg.ws_url[0];
}

static void voice_close_transport_locked(void)
{
    esp_websocket_client_handle_t client = s_ws_client;

    s_ws_client = NULL;
    s_ws_connected = false;
    s_session_ready = false;
    s_start_session_requested = false;
    xEventGroupClearBits(s_events, VOICE_EVENT_WS_CONNECTED | VOICE_EVENT_SESSION_READY);

    if (client) {
        if (esp_websocket_client_is_connected(client)) {
            (void)esp_websocket_client_stop(client);
        }
        (void)esp_websocket_client_destroy(client);
    }
}

static esp_err_t voice_build_headers_locked(char *buf, size_t size)
{
    int written;

    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(
        buf, size,
        "X-Api-Resource-Id: " VOICE_RESOURCE_ID "\r\n"
        "X-Api-App-ID: %s\r\n"
        "X-Api-App-Key: " VOICE_COMPAT_APP_KEY "\r\n"
        "X-Api-Access-Key: %s\r\n"
        "Authorization: Bearer;%s\r\n"
        "X-Api-Connect-Id: %s\r\n",
        s_cfg.appid,
        s_cfg.access_token,
        s_cfg.access_token,
        s_connect_id);
    if (written < 0 || (size_t)written >= size) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_cfg.cluster[0]) {
        int extra = snprintf(buf + written, size - (size_t)written,
                             "X-Api-Cluster: %s\r\n", s_cfg.cluster);
        if (extra < 0 || (size_t)extra >= size - (size_t)written) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    return ESP_OK;
}

static esp_err_t voice_send_binary_locked(const uint8_t *data, size_t size)
{
    int sent;

    if (!s_ws_client || !s_ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    sent = esp_websocket_client_send_with_opcode(
        s_ws_client, WS_TRANSPORT_OPCODES_BINARY, data, (int)size, pdMS_TO_TICKS(2000));
    return (sent >= 0 && (size_t)sent == size) ? ESP_OK : ESP_FAIL;
}

static esp_err_t voice_send_control_json_locked(cJSON *root)
{
    uint8_t header[4];
    uint8_t *packet = NULL;
    char *json_str = NULL;
    size_t payload_len;
    size_t packet_len;
    int seq;
    esp_err_t err = ESP_OK;

    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    payload_len = strlen(json_str);
    packet_len = sizeof(header) + 4 + 4 + payload_len;
    packet = (uint8_t *)malloc(packet_len);
    if (!packet) {
        free(json_str);
        return ESP_ERR_NO_MEM;
    }

    seq = s_next_sequence++;
    header[0] = (uint8_t)((VOICE_WS_PROTO_VERSION << 4) | VOICE_WS_HEADER_SIZE);
    header[1] = (uint8_t)((VOICE_MSG_FULL_CLIENT_REQ << 4) | VOICE_FLAG_HAS_SEQUENCE);
    header[2] = (uint8_t)((VOICE_SERIAL_JSON << 4) | VOICE_COMPRESS_NONE);
    header[3] = 0x00;

    memcpy(packet, header, sizeof(header));
    packet[4] = (uint8_t)((seq >> 24) & 0xFF);
    packet[5] = (uint8_t)((seq >> 16) & 0xFF);
    packet[6] = (uint8_t)((seq >> 8) & 0xFF);
    packet[7] = (uint8_t)(seq & 0xFF);
    packet[8] = (uint8_t)((payload_len >> 24) & 0xFF);
    packet[9] = (uint8_t)((payload_len >> 16) & 0xFF);
    packet[10] = (uint8_t)((payload_len >> 8) & 0xFF);
    packet[11] = (uint8_t)(payload_len & 0xFF);
    memcpy(packet + 12, json_str, payload_len);

    err = voice_send_binary_locked(packet, packet_len);
    free(packet);
    free(json_str);
    return err;
}

static esp_err_t voice_send_start_session_locked(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *user = cJSON_CreateObject();
    cJSON *audio = cJSON_CreateObject();
    cJSON *dialog = cJSON_CreateObject();
    cJSON *conversation = cJSON_CreateObject();
    esp_err_t err;

    if (!root || !user || !audio || !dialog || !conversation) {
        cJSON_Delete(root);
        cJSON_Delete(user);
        cJSON_Delete(audio);
        cJSON_Delete(dialog);
        cJSON_Delete(conversation);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "StartSession");
    cJSON_AddNumberToObject(root, "event", 100);
    cJSON_AddStringToObject(user, "uid", s_connect_id[0] ? s_connect_id : MIMI_VOICE_DEFAULT_CHAT_ID);
    cJSON_AddStringToObject(dialog, "bot_name", VOICE_DEFAULT_BOT_NAME);
    if (s_cfg.cluster[0]) {
        cJSON_AddStringToObject(dialog, "cluster", s_cfg.cluster);
    }
    if (s_cfg.language[0]) {
        cJSON_AddStringToObject(dialog, "language", s_cfg.language);
        cJSON_AddStringToObject(conversation, "language", s_cfg.language);
    }

    cJSON_AddStringToObject(audio, "format", "pcm");
    cJSON_AddStringToObject(audio, "codec", "raw");
    cJSON_AddNumberToObject(audio, "rate", MIMI_AUDIO_INPUT_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio, "bits", MIMI_AUDIO_INPUT_BITS);
    cJSON_AddNumberToObject(audio, "channel", 1);

    cJSON_AddItemToObject(root, "user", user);
    cJSON_AddItemToObject(root, "audio", audio);
    cJSON_AddItemToObject(root, "dialog", dialog);
    cJSON_AddItemToObject(root, "conversation", conversation);

    err = voice_send_control_json_locked(root);
    cJSON_Delete(root);
    return err;
}

static esp_err_t voice_send_audio_chunk_locked(const int16_t *samples, size_t sample_count, bool final_chunk)
{
    uint8_t header[4];
    uint8_t *packet = NULL;
    size_t payload_len = sample_count * sizeof(int16_t);
    size_t packet_len = sizeof(header) + 4 + 4 + payload_len;
    int seq = s_next_sequence++;
    int sent_seq = final_chunk ? -seq : seq;
    esp_err_t err;

    packet = (uint8_t *)malloc(packet_len);
    if (!packet) {
        return ESP_ERR_NO_MEM;
    }

    header[0] = (uint8_t)((VOICE_WS_PROTO_VERSION << 4) | VOICE_WS_HEADER_SIZE);
    header[1] = (uint8_t)((VOICE_MSG_AUDIO_ONLY_REQ << 4) |
                          (final_chunk ? VOICE_FLAG_NEG_SEQUENCE : VOICE_FLAG_HAS_SEQUENCE));
    header[2] = (uint8_t)((VOICE_SERIAL_NONE << 4) | VOICE_COMPRESS_NONE);
    header[3] = 0x00;

    memcpy(packet, header, sizeof(header));
    packet[4] = (uint8_t)((sent_seq >> 24) & 0xFF);
    packet[5] = (uint8_t)((sent_seq >> 16) & 0xFF);
    packet[6] = (uint8_t)((sent_seq >> 8) & 0xFF);
    packet[7] = (uint8_t)(sent_seq & 0xFF);
    packet[8] = (uint8_t)((payload_len >> 24) & 0xFF);
    packet[9] = (uint8_t)((payload_len >> 16) & 0xFF);
    packet[10] = (uint8_t)((payload_len >> 8) & 0xFF);
    packet[11] = (uint8_t)(payload_len & 0xFF);
    if (payload_len > 0 && samples) {
        memcpy(packet + 12, samples, payload_len);
    }

    err = voice_send_binary_locked(packet, packet_len);
    free(packet);
    return err;
}

static bool voice_parse_frame(const uint8_t *data, size_t len, voice_frame_t *frame)
{
    size_t header_bytes;
    const uint8_t *payload;
    size_t payload_len;

    if (!data || !frame || len < 4) {
        return false;
    }
    if ((data[0] >> 4) != VOICE_WS_PROTO_VERSION) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    header_bytes = (size_t)(data[0] & 0x0F) * 4U;
    if (header_bytes < 4 || header_bytes > len) {
        return false;
    }

    frame->message_type = (uint8_t)(data[1] >> 4);
    frame->flags = (uint8_t)(data[1] & 0x0F);
    frame->serialization = (uint8_t)(data[2] >> 4);
    frame->compression = (uint8_t)(data[2] & 0x0F);
    frame->last_package = (frame->flags & VOICE_FLAG_LAST_PACKAGE) != 0;

    payload = data + header_bytes;
    payload_len = len - header_bytes;

    if (frame->flags & VOICE_FLAG_HAS_SEQUENCE) {
        if (payload_len < 4) {
            return false;
        }
        frame->outer_sequence = (int)((payload[0] << 24) | (payload[1] << 16) |
                                      (payload[2] << 8) | payload[3]);
        payload += 4;
        payload_len -= 4;
    }

    switch (frame->message_type) {
        case VOICE_MSG_FULL_SERVER_RESP:
        case VOICE_MSG_FRONTEND_RESP:
            if (payload_len < 4) {
                return false;
            }
            frame->payload_size = (uint32_t)((payload[0] << 24) | (payload[1] << 16) |
                                             (payload[2] << 8) | payload[3]);
            frame->payload = payload + 4;
            frame->payload_len = payload_len - 4;
            return true;

        case VOICE_MSG_SERVER_ACK:
            if (payload_len < 8) {
                return false;
            }
            frame->ack_sequence = (int)((payload[0] << 24) | (payload[1] << 16) |
                                        (payload[2] << 8) | payload[3]);
            frame->payload_size = (uint32_t)((payload[4] << 24) | (payload[5] << 16) |
                                             (payload[6] << 8) | payload[7]);
            frame->payload = payload + 8;
            frame->payload_len = payload_len - 8;
            return true;

        case VOICE_MSG_SERVER_ERROR:
            if (payload_len < 8) {
                return false;
            }
            frame->error_code = (int)((payload[0] << 24) | (payload[1] << 16) |
                                      (payload[2] << 8) | payload[3]);
            frame->payload_size = (uint32_t)((payload[4] << 24) | (payload[5] << 16) |
                                             (payload[6] << 8) | payload[7]);
            frame->payload = payload + 8;
            frame->payload_len = payload_len - 8;
            return true;

        default:
            frame->payload = payload;
            frame->payload_len = payload_len;
            frame->payload_size = (uint32_t)payload_len;
            return true;
    }
}

static void voice_handle_audio_payload_locked(const uint8_t *payload, size_t len, bool last_package)
{
    if (!payload || len < 2) {
        if (last_package) {
            voice_set_state_locked("idle", "Voice", "Ready", "circle_check");
        }
        return;
    }

    if ((payload[0] == 'R' && payload[1] == 'I') ||
        (payload[0] == 'I' && payload[1] == 'D') ||
        (payload[0] == 'O' && payload[1] == 'g')) {
        voice_set_error_locked("Compressed audio payload unsupported");
        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
        return;
    }

    if (audio_hal_output_is_available()) {
        (void)audio_hal_output_set_sample_rate(s_remote_sample_rate);
        if (audio_hal_output_write_pcm16((const int16_t *)payload, len / sizeof(int16_t), 100) != ESP_OK) {
            voice_set_error_locked("Speaker write failed");
            voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
            return;
        }
    }

    voice_set_state_locked("speaking",
                           s_partial_assistant_text[0] ? "Reply" : "Voice",
                           s_partial_assistant_text[0] ? s_partial_assistant_text : "Playing response",
                           "volume_high");
    if (last_package) {
        if (s_partial_assistant_text[0]) {
            voice_session_append_locked("assistant", s_partial_assistant_text);
        }
        voice_set_state_locked("idle", "Voice", "Ready", "circle_check");
    }
}

static void voice_handle_json_locked(cJSON *root)
{
    cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    cJSON *session_id = cJSON_GetObjectItemCaseSensitive(root, "session_id");
    const char *user_text;
    const char *assistant_text;
    const char *generic_keys[] = {"final", "is_final", "end", "ended", "done"};
    bool final_flag;

    if (cJSON_IsString(session_id) && session_id->valuestring && session_id->valuestring[0]) {
        voice_copy_text(s_session_id, sizeof(s_session_id), session_id->valuestring);
    }

    if (cJSON_IsNumber(event)) {
        if (event->valueint == 50) {
            voice_set_notice_locked("WebSocket connected");
        } else if (event->valueint == 150) {
            s_session_ready = true;
            xEventGroupSetBits(s_events, VOICE_EVENT_SESSION_READY);
            voice_set_notice_locked("Session started");
            voice_set_state_locked(s_capture_requested ? (s_ptt_active ? "listening_ptt" : "listening_toggle") : "idle",
                                   s_capture_requested ? "Listening" : "Voice",
                                   s_capture_requested ? "Capture started" : "Ready",
                                   s_capture_requested ? "microphone" : "circle_check");
        }
    }

    voice_maybe_update_sample_rate_locked(root);
    final_flag = voice_json_flag_true(root, generic_keys, sizeof(generic_keys) / sizeof(generic_keys[0]));

    assistant_text = voice_extract_assistant_text(root);
    user_text = voice_extract_user_text(root);

    if (!assistant_text && !user_text) {
        const char *generic_text_keys[] = {"text", "content", "message"};
        const char *generic_text = voice_find_text_by_value_keys(root, generic_text_keys,
                                                                 sizeof(generic_text_keys) / sizeof(generic_text_keys[0]));
        if (generic_text && generic_text[0]) {
            if (s_capture_requested || s_ptt_active ||
                voice_contains_nocase(s_state, "listening") ||
                voice_contains_nocase(s_state, "thinking")) {
                user_text = generic_text;
            } else {
                assistant_text = generic_text;
            }
        }
    }

    if (user_text && user_text[0]) {
        voice_note_user_text_locked(user_text, final_flag);
    }
    if (assistant_text && assistant_text[0]) {
        voice_note_assistant_text_locked(assistant_text, final_flag);
    }
}

static void voice_handle_text_message_locked(const char *text, size_t len)
{
    cJSON *root;

    if (!text || len == 0) {
        return;
    }

    root = cJSON_ParseWithLength(text, len);
    if (!root) {
        return;
    }

    voice_handle_json_locked(root);
    cJSON_Delete(root);
}

static void voice_handle_binary_message_locked(const uint8_t *data, size_t len)
{
    voice_frame_t frame;

    if (!voice_parse_frame(data, len, &frame)) {
        voice_handle_audio_payload_locked(data, len, false);
        return;
    }

    if (frame.message_type == VOICE_MSG_SERVER_ERROR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Server error code=%d", frame.error_code);
        voice_set_error_locked(msg);
        voice_set_state_locked("error", "Voice error", msg, "triangle_exclamation");
        return;
    }

    if (frame.serialization == VOICE_SERIAL_JSON && frame.payload && frame.payload_len > 0) {
        cJSON *root = cJSON_ParseWithLength((const char *)frame.payload, frame.payload_len);
        if (root) {
            voice_handle_json_locked(root);
            cJSON_Delete(root);
        }
        return;
    }

    if (frame.message_type == VOICE_MSG_SERVER_ACK && frame.payload && frame.payload_len > 0) {
        voice_handle_audio_payload_locked(frame.payload, frame.payload_len,
                                          frame.last_package || frame.ack_sequence < 0);
    }
}

static void voice_ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *e = (esp_websocket_event_data_t *)event_data;
    (void)arg;
    (void)base;

    if (!s_lock) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            s_ws_connected = true;
            s_transport_faulted = false;
            xEventGroupSetBits(s_events, VOICE_EVENT_WS_CONNECTED);
            voice_clear_error_locked();
            s_start_session_requested = true;
            voice_set_state_locked("connecting", "Voice", "Connected, starting session", "satellite_dish");
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
            s_ws_connected = false;
            s_session_ready = false;
            s_transport_faulted = true;
            xEventGroupClearBits(s_events, VOICE_EVENT_WS_CONNECTED | VOICE_EVENT_SESSION_READY);
            break;

        case WEBSOCKET_EVENT_ERROR:
            s_transport_faulted = true;
            voice_set_error_locked("WebSocket transport error");
            voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
            break;

        case WEBSOCKET_EVENT_DATA:
            if (e->payload_offset == 0) {
                free(s_rx_buf);
                s_rx_buf = NULL;
                s_rx_cap = 0;
                if (e->payload_len > 0) {
                    s_rx_buf = (uint8_t *)malloc((size_t)e->payload_len);
                    if (s_rx_buf) {
                        s_rx_cap = (size_t)e->payload_len;
                    }
                }
            }

            if (s_rx_buf && (size_t)(e->payload_offset + e->data_len) <= s_rx_cap) {
                memcpy(s_rx_buf + e->payload_offset, e->data_ptr, (size_t)e->data_len);
                if (e->payload_offset + e->data_len >= e->payload_len) {
                    if (e->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                        voice_handle_text_message_locked((const char *)s_rx_buf, (size_t)e->payload_len);
                    } else {
                        voice_handle_binary_message_locked(s_rx_buf, (size_t)e->payload_len);
                    }
                    free(s_rx_buf);
                    s_rx_buf = NULL;
                    s_rx_cap = 0;
                }
            }
            break;

        default:
            break;
    }

    xSemaphoreGive(s_lock);
}

static esp_err_t voice_open_transport_locked(void)
{
    esp_websocket_client_config_t ws_cfg = {0};
    static char headers[VOICE_WS_HEADERS_MAX];
    esp_err_t err;

    if (s_ws_client) {
        return ESP_OK;
    }
    if (!voice_has_credentials_locked()) {
        voice_set_error_locked("Voice credentials missing");
        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
        return ESP_ERR_INVALID_STATE;
    }

    voice_generate_connect_id_locked();
    err = voice_build_headers_locked(headers, sizeof(headers));
    if (err != ESP_OK) {
        voice_set_error_locked("Voice headers too large");
        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
        return err;
    }

    ws_cfg.uri = s_cfg.ws_url;
    ws_cfg.headers = headers;
    ws_cfg.buffer_size = MIMI_VOICE_WS_BUFFER;
    ws_cfg.task_stack = 8192;
    ws_cfg.network_timeout_ms = 10000;
    ws_cfg.reconnect_timeout_ms = 5000;
    ws_cfg.disable_auto_reconnect = true;
    ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_client) {
        voice_set_error_locked("esp_websocket_client_init failed");
        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, voice_ws_event_handler, NULL);
    err = esp_websocket_client_start(s_ws_client);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        voice_set_error_locked("WebSocket start failed");
        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
        return err;
    }

    voice_set_notice_locked("Opening voice websocket");
    voice_set_state_locked("connecting", "Voice", "Connecting to Doubao", "satellite_dish");
    return ESP_OK;
}

static void voice_channel_task(void *arg)
{
    int16_t mic_samples[VOICE_AUDIO_READ_SAMPLES];

    (void)arg;
    while (1) {
        bool need_session = false;
        bool send_eos = false;
        bool capture = false;
        TickType_t now = xTaskGetTickCount();

        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_transport_faulted) {
                voice_close_transport_locked();
                s_transport_faulted = false;
                s_next_connect_tick = (uint32_t)now + pdMS_TO_TICKS(1500);
            }

            need_session = s_connect_test_requested || s_capture_requested;
            send_eos = s_end_of_input_requested && s_session_ready && s_ws_connected;
            capture = s_capture_requested && s_session_ready && s_ws_connected;

            if (need_session && !s_ws_client && (uint32_t)now >= s_next_connect_tick) {
                if (voice_open_transport_locked() != ESP_OK) {
                    s_next_connect_tick = (uint32_t)now + pdMS_TO_TICKS(3000);
                }
            }

            if (s_ws_connected && s_start_session_requested && !s_session_ready) {
                if (voice_send_start_session_locked() == ESP_OK) {
                    s_start_session_requested = false;
                    voice_set_notice_locked("StartSession sent");
                } else {
                    voice_set_error_locked("StartSession send failed");
                    voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
                }
            }

            if (send_eos) {
                if (voice_send_audio_chunk_locked(NULL, 0, true) == ESP_OK) {
                    s_end_of_input_requested = false;
                    voice_set_notice_locked("End of input sent");
                    if (s_partial_user_text[0]) {
                        voice_session_append_locked("user", s_partial_user_text);
                    }
                    voice_set_state_locked("thinking", "Voice", "Waiting for reply", "brain");
                } else {
                    voice_set_error_locked("Failed to send end-of-input");
                    voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
                }
            }

            xSemaphoreGive(s_lock);
        }

        if (capture) {
            size_t sample_count = 0;
            esp_err_t mic_err = audio_hal_input_read_pcm16(mic_samples, VOICE_AUDIO_READ_SAMPLES,
                                                           &sample_count, 60);
            if (mic_err == ESP_OK && sample_count > 0) {
                if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    if (voice_send_audio_chunk_locked(mic_samples, sample_count, false) != ESP_OK) {
                        voice_set_error_locked("Failed to stream microphone audio");
                        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
                    }
                    xSemaphoreGive(s_lock);
                }
            } else if (mic_err != ESP_ERR_TIMEOUT) {
                if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    voice_set_error_locked("Microphone capture failed");
                    voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
                    xSemaphoreGive(s_lock);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

esp_err_t voice_channel_init(void)
{
    esp_err_t speaker_err;

    if (s_initialized) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) {
        return ESP_ERR_NO_MEM;
    }

    speaker_err = audio_hal_output_init();
    if (speaker_err != ESP_OK) {
        ESP_LOGW(TAG, "Speaker output unavailable: %s", esp_err_to_name(speaker_err));
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    voice_load_config_locked();
    s_continuous_mode = s_cfg.continuous_mode_default;
    voice_copy_text(s_session_id, sizeof(s_session_id), MIMI_VOICE_DEFAULT_CHAT_ID);
    voice_reset_buffers_locked();
    voice_set_state_locked("idle", "Voice", "Ready", "circle_check");
    xSemaphoreGive(s_lock);

    (void)button_service_set_continuous_mode(s_continuous_mode);
    if (xTaskCreate(voice_channel_task, "voice_channel", 8192, NULL, 5, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Voice channel initialized: ws=%s lang=%s continuous=%s",
             s_cfg.ws_url, s_cfg.language, s_continuous_mode ? "on" : "off");
    return ESP_OK;
}

bool voice_channel_is_initialized(void)
{
    return s_initialized;
}

esp_err_t voice_channel_set_continuous_mode(bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_continuous_mode = enabled;
    if (enabled) {
        s_capture_requested = true;
        s_end_of_input_requested = false;
        s_ptt_active = false;
        voice_set_state_locked(s_session_ready ? "listening_toggle" : "connecting",
                               "Continuous ON", "Streaming microphone", "microphone");
    } else {
        s_capture_requested = false;
        s_ptt_active = false;
        s_end_of_input_requested = s_session_ready;
        voice_set_state_locked(s_session_ready ? "thinking" : "idle",
                               "Continuous OFF",
                               s_session_ready ? "Stopping microphone" : "Voice ready",
                               s_session_ready ? "brain" : "circle_check");
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool voice_channel_get_continuous_mode(void)
{
    return s_continuous_mode;
}

esp_err_t voice_channel_set_ptt(bool pressed)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (pressed) {
        s_ptt_active = true;
        s_capture_requested = true;
        s_end_of_input_requested = false;
        voice_set_state_locked(s_session_ready ? "listening_ptt" : "connecting",
                               "PTT", "Microphone capturing", "microphone");
    } else {
        s_ptt_active = false;
        s_capture_requested = false;
        s_end_of_input_requested = s_session_ready;
        voice_set_state_locked(s_session_ready ? "thinking" : "idle",
                               "PTT Ended",
                               s_session_ready ? "Waiting for reply" : "Voice ready",
                               s_session_ready ? "brain" : "circle_check");
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool voice_channel_get_ptt_active(void)
{
    return s_ptt_active;
}

esp_err_t voice_channel_connect_test(uint32_t timeout_ms)
{
    EventBits_t bits;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0) {
        timeout_ms = 10000;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    voice_load_config_locked();
    if (!voice_has_credentials_locked()) {
        voice_set_error_locked("Voice appid/token/ws_url missing");
        voice_set_state_locked("error", "Voice error", s_last_error, "triangle_exclamation");
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_connect_test_requested = true;
    s_start_session_requested = true;
    s_next_connect_tick = 0;
    xEventGroupClearBits(s_events, VOICE_EVENT_SESSION_READY | VOICE_EVENT_ERROR);
    voice_set_state_locked("connecting", "Voice", "Running connect test", "satellite_dish");
    xSemaphoreGive(s_lock);

    bits = xEventGroupWaitBits(
        s_events, VOICE_EVENT_SESSION_READY | VOICE_EVENT_ERROR,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & VOICE_EVENT_SESSION_READY) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t voice_channel_reset(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_capture_requested = false;
    s_ptt_active = false;
    s_end_of_input_requested = false;
    s_connect_test_requested = false;
    s_transport_faulted = false;
    voice_load_config_locked();
    s_continuous_mode = s_cfg.continuous_mode_default;
    voice_reset_buffers_locked();
    voice_copy_text(s_session_id, sizeof(s_session_id), MIMI_VOICE_DEFAULT_CHAT_ID);
    voice_close_transport_locked();
    voice_clear_error_locked();
    voice_set_notice_locked("Voice reset complete");
    voice_set_state_locked("idle", "Voice", "Ready", "circle_check");
    xSemaphoreGive(s_lock);

    (void)audio_hal_output_set_sample_rate(MIMI_AUDIO_OUTPUT_SAMPLE_RATE);
    return ESP_OK;
}

const char *voice_channel_get_state(void)
{
    return s_state;
}

esp_err_t voice_channel_format_info(char *buf, size_t size)
{
    char speaker_info[256] = {0};

    if (!buf || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)audio_hal_output_format_info(speaker_info, sizeof(speaker_info));
    snprintf(buf, size,
             "state=%s ws=%s session=%s continuous=%s ptt=%s ws_url=%s appid=%s lang=%s cluster=%s remote_rate=%d last_error=%s speaker={%s}",
             s_state,
             s_ws_connected ? "connected" : "disconnected",
             s_session_ready ? "ready" : "pending",
             s_continuous_mode ? "on" : "off",
             s_ptt_active ? "on" : "off",
             s_cfg.ws_url[0] ? s_cfg.ws_url : "(empty)",
             s_cfg.appid[0] ? s_cfg.appid : "(empty)",
             s_cfg.language[0] ? s_cfg.language : "(empty)",
             s_cfg.cluster[0] ? s_cfg.cluster : "(empty)",
             s_remote_sample_rate,
             s_last_error[0] ? s_last_error : "(none)",
             speaker_info);
    return ESP_OK;
}
