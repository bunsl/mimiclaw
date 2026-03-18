#include "llm_proxy.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "llm";

#define LLM_API_KEY_MAX_LEN 320
#define LLM_MODEL_MAX_LEN   64
#define LLM_PROVIDER_MAX_LEN 16
#define LLM_DUMP_MAX_BYTES   (16 * 1024)
#define LLM_DUMP_CHUNK_BYTES 320

static char s_api_key[LLM_API_KEY_MAX_LEN] = {0};
static char s_model[LLM_MODEL_MAX_LEN] = MIMI_LLM_DEFAULT_MODEL;
static char s_provider[LLM_PROVIDER_MAX_LEN] = MIMI_LLM_PROVIDER_DEFAULT;

static void llm_log_payload(const char *label, const char *payload)
{
    if (!payload) {
        ESP_LOGI(TAG, "%s: <null>", label);
        return;
    }

    size_t total = strlen(payload);
#if MIMI_LLM_LOG_VERBOSE_PAYLOAD
    size_t shown = total > LLM_DUMP_MAX_BYTES ? LLM_DUMP_MAX_BYTES : total;
    ESP_LOGI(TAG, "%s (%u bytes)%s",
             label,
             (unsigned)total,
             (shown < total) ? " [truncated]" : "");

    char chunk[LLM_DUMP_CHUNK_BYTES + 1];
    for (size_t off = 0; off < shown; off += LLM_DUMP_CHUNK_BYTES) {
        size_t n = shown - off;
        if (n > LLM_DUMP_CHUNK_BYTES) {
            n = LLM_DUMP_CHUNK_BYTES;
        }
        memcpy(chunk, payload + off, n);
        chunk[n] = '\0';
        ESP_LOGI(TAG, "%s[%u]: %s", label, (unsigned)off, chunk);
    }
#else
    if (MIMI_LLM_LOG_PREVIEW_BYTES > 0) {
        size_t shown = total > MIMI_LLM_LOG_PREVIEW_BYTES ? MIMI_LLM_LOG_PREVIEW_BYTES : total;
        char preview[MIMI_LLM_LOG_PREVIEW_BYTES + 1];
        memcpy(preview, payload, shown);
        preview[shown] = '\0';
        for (size_t i = 0; i < shown; i++) {
            if (preview[i] == '\n' || preview[i] == '\r' || preview[i] == '\t') {
                preview[i] = ' ';
            }
        }
        ESP_LOGI(TAG, "%s (%u bytes): %s%s",
                 label,
                 (unsigned)total,
                 preview,
                 (shown < total) ? " ..." : "");
    } else {
        ESP_LOGI(TAG, "%s (%u bytes)", label, (unsigned)total);
    }
#endif
}

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, dst_size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ── Response buffer ──────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} resp_buf_t;

static esp_err_t resp_buf_init(resp_buf_t *rb, size_t initial_cap)
{
    rb->data = heap_caps_calloc(1, initial_cap, MALLOC_CAP_SPIRAM);
    if (!rb->data) return ESP_ERR_NO_MEM;
    rb->len = 0;
    rb->cap = initial_cap;
    return ESP_OK;
}

static esp_err_t resp_buf_append(resp_buf_t *rb, const char *data, size_t len)
{
    while (rb->len + len >= rb->cap) {
        size_t new_cap = rb->cap * 2;
        char *tmp = heap_caps_realloc(rb->data, new_cap, MALLOC_CAP_SPIRAM);
        if (!tmp) return ESP_ERR_NO_MEM;
        rb->data = tmp;
        rb->cap = new_cap;
    }
    memcpy(rb->data + rb->len, data, len);
    rb->len += len;
    rb->data[rb->len] = '\0';
    return ESP_OK;
}

static void resp_buf_free(resp_buf_t *rb)
{
    free(rb->data);
    rb->data = NULL;
    rb->len = 0;
    rb->cap = 0;
}

/* ── Chunked transfer encoding decoder ───────────────────────── */

static void resp_buf_decode_chunked(resp_buf_t *rb)
{
    if (!rb->data || rb->len == 0) return;

    /* Quick check: if body starts with '{' or '[', it's not chunked */
    size_t i = 0;
    while (i < rb->len && (rb->data[i] == ' ' || rb->data[i] == '\t')) i++;
    if (i < rb->len && (rb->data[i] == '{' || rb->data[i] == '[')) return;

    /* Try to decode chunked encoding in-place */
    char *src = rb->data;
    char *dst = rb->data;
    char *end = rb->data + rb->len;

    while (src < end) {
        /* Parse hex chunk size */
        char *line_end = strstr(src, "\r\n");
        if (!line_end) break;

        unsigned long chunk_size = strtoul(src, NULL, 16);
        if (chunk_size == 0) break;  /* terminal chunk */

        src = line_end + 2;  /* skip past \r\n after size */

        if (src + chunk_size > end) {
            /* Incomplete chunk, copy what we have */
            size_t avail = end - src;
            memmove(dst, src, avail);
            dst += avail;
            break;
        }

        memmove(dst, src, chunk_size);
        dst += chunk_size;
        src += chunk_size;

        /* Skip trailing \r\n after chunk data */
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') {
            src += 2;
        }
    }

    rb->len = dst - rb->data;
    rb->data[rb->len] = '\0';
}

/* ── HTTP event handler (for esp_http_client direct path) ─────── */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = (resp_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        resp_buf_append(rb, (const char *)evt->data, evt->data_len);
    }
    return ESP_OK;
}

/* ── Provider helpers ──────────────────────────────────────────── */

static bool provider_is_openai(const char *provider)
{
    return provider && strcmp(provider, "openai") == 0;
}

static bool provider_is_minimax(const char *provider)
{
    return provider && strcmp(provider, "minimax") == 0;
}

static bool provider_is_volcengine(const char *provider)
{
    return provider &&
           (strcmp(provider, "volcengine") == 0 ||
            strcmp(provider, "ark") == 0 ||
            strcmp(provider, "doubao") == 0);
}

static bool provider_is_openai_compatible(const char *provider)
{
    return provider_is_openai(provider) ||
           provider_is_minimax(provider) ||
           provider_is_volcengine(provider);
}

static const char *provider_canonical_name(const char *provider)
{
    if (!provider) {
        return "";
    }
    if (strcmp(provider, "ark") == 0 || strcmp(provider, "doubao") == 0) {
        return "volcengine";
    }
    return provider;
}

typedef struct {
    const char *channel;
    const char *api_key_nvs_key;
    const char *api_key_build;
    const char *model_nvs_key;
    const char *model_build;
    const char *provider_nvs_key;
    const char *provider_build;
} channel_llm_config_t;

typedef struct {
    char api_key[LLM_API_KEY_MAX_LEN];
    char model[LLM_MODEL_MAX_LEN];
    char provider[LLM_PROVIDER_MAX_LEN];
} llm_runtime_config_t;

static const channel_llm_config_t *channel_llm_config_for(const char *channel)
{
    static const channel_llm_config_t configs[] = {
        { MIMI_CHAN_FEISHU,    MIMI_NVS_KEY_API_KEY_FEISHU, MIMI_SECRET_FEISHU_API_KEY,
                               MIMI_NVS_KEY_MODEL_FEISHU,   MIMI_SECRET_FEISHU_MODEL,
                               MIMI_NVS_KEY_PROVIDER_FEISHU, MIMI_SECRET_FEISHU_MODEL_PROVIDER },
        { MIMI_CHAN_WEBSOCKET, MIMI_NVS_KEY_API_KEY_WS,     MIMI_SECRET_WEBSOCKET_API_KEY,
                               MIMI_NVS_KEY_MODEL_WS,       MIMI_SECRET_WEBSOCKET_MODEL,
                               MIMI_NVS_KEY_PROVIDER_WS,    MIMI_SECRET_WEBSOCKET_MODEL_PROVIDER },
        { MIMI_CHAN_CLI,       MIMI_NVS_KEY_API_KEY_CLI,    MIMI_SECRET_CLI_API_KEY,
                               MIMI_NVS_KEY_MODEL_CLI,      MIMI_SECRET_CLI_MODEL,
                               MIMI_NVS_KEY_PROVIDER_CLI,   MIMI_SECRET_CLI_MODEL_PROVIDER },
        { MIMI_CHAN_SYSTEM,    MIMI_NVS_KEY_API_KEY_SYSTEM, MIMI_SECRET_SYSTEM_API_KEY,
                               MIMI_NVS_KEY_MODEL_SYSTEM,   MIMI_SECRET_SYSTEM_MODEL,
                               MIMI_NVS_KEY_PROVIDER_SYSTEM, MIMI_SECRET_SYSTEM_MODEL_PROVIDER },
    };

    if (!channel || channel[0] == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(configs) / sizeof(configs[0]); i++) {
        if (strcmp(channel, configs[i].channel) == 0) {
            return &configs[i];
        }
    }
    return NULL;
}

static void llm_read_channel_override_string(const char *nvs_key,
                                             const char *build_val,
                                             char *value,
                                             size_t value_size)
{
    if (!value || value_size == 0) {
        return;
    }
    value[0] = '\0';

    if (build_val && build_val[0] != '\0') {
        safe_copy(value, value_size, build_val);
    }

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_LLM, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[LLM_API_KEY_MAX_LEN] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, nvs_key, tmp, &len) == ESP_OK && tmp[0] != '\0') {
            safe_copy(value, value_size, tmp);
        }
        nvs_close(nvs);
    }
}

static void llm_get_channel_api_key_override(const char *channel, char *api_key, size_t api_key_size)
{
    if (!api_key || api_key_size == 0) {
        return;
    }
    api_key[0] = '\0';

    const channel_llm_config_t *cfg = channel_llm_config_for(channel);
    if (!cfg) {
        return;
    }

    llm_read_channel_override_string(cfg->api_key_nvs_key, cfg->api_key_build, api_key, api_key_size);
}

static void llm_get_channel_model_override(const char *channel, char *model, size_t model_size)
{
    if (!model || model_size == 0) {
        return;
    }
    model[0] = '\0';

    const channel_llm_config_t *cfg = channel_llm_config_for(channel);
    if (!cfg) {
        return;
    }

    llm_read_channel_override_string(cfg->model_nvs_key, cfg->model_build, model, model_size);
}

static void llm_get_channel_provider_override(const char *channel, char *provider, size_t provider_size)
{
    if (!provider || provider_size == 0) {
        return;
    }
    provider[0] = '\0';

    const channel_llm_config_t *cfg = channel_llm_config_for(channel);
    if (!cfg) {
        return;
    }

    llm_read_channel_override_string(cfg->provider_nvs_key, cfg->provider_build, provider, provider_size);
    if (provider[0] != '\0') {
        safe_copy(provider, provider_size, provider_canonical_name(provider));
    }
}

static void llm_get_effective_config_for_channel(const char *channel, llm_runtime_config_t *cfg)
{
    if (!cfg) {
        return;
    }

    safe_copy(cfg->api_key, sizeof(cfg->api_key), s_api_key);
    safe_copy(cfg->model, sizeof(cfg->model), s_model);
    safe_copy(cfg->provider, sizeof(cfg->provider), s_provider);

    char override_api_key[LLM_API_KEY_MAX_LEN] = {0};
    char override_model[LLM_MODEL_MAX_LEN] = {0};
    char override_provider[LLM_PROVIDER_MAX_LEN] = {0};

    llm_get_channel_api_key_override(channel, override_api_key, sizeof(override_api_key));
    llm_get_channel_model_override(channel, override_model, sizeof(override_model));
    llm_get_channel_provider_override(channel, override_provider, sizeof(override_provider));

    if (override_api_key[0] != '\0') {
        safe_copy(cfg->api_key, sizeof(cfg->api_key), override_api_key);
    }
    if (override_model[0] != '\0') {
        safe_copy(cfg->model, sizeof(cfg->model), override_model);
    }
    if (override_provider[0] != '\0') {
        safe_copy(cfg->provider, sizeof(cfg->provider), override_provider);
    }
}

static const char *llm_api_url(const char *provider)
{
    if (provider_is_openai(provider)) {
        return MIMI_OPENAI_API_URL;
    }
    if (provider_is_minimax(provider)) {
        return MIMI_MINIMAX_API_URL;
    }
    if (provider_is_volcengine(provider)) {
        return MIMI_VOLCENGINE_API_URL;
    }
    return MIMI_LLM_API_URL;
}

static const char *llm_api_host(const char *provider)
{
    if (provider_is_openai(provider)) {
        return "api.openai.com";
    }
    if (provider_is_minimax(provider)) {
        return "api.minimaxi.com";
    }
    if (provider_is_volcengine(provider)) {
        return "ark.cn-beijing.volces.com";
    }
    return "api.anthropic.com";
}

static const char *llm_api_path(const char *provider)
{
    if (provider_is_volcengine(provider)) {
        return "/api/v3/chat/completions";
    }
    if (provider_is_openai_compatible(provider)) {
        return "/v1/chat/completions";
    }
    return "/v1/messages";
}

/* ── Init ─────────────────────────────────────────────────────── */

esp_err_t llm_proxy_init(void)
{
    /* Start with build-time defaults */
    if (MIMI_SECRET_API_KEY[0] != '\0') {
        safe_copy(s_api_key, sizeof(s_api_key), MIMI_SECRET_API_KEY);
    }
    if (MIMI_SECRET_MODEL[0] != '\0') {
        safe_copy(s_model, sizeof(s_model), MIMI_SECRET_MODEL);
    }
    if (MIMI_SECRET_MODEL_PROVIDER[0] != '\0') {
        safe_copy(s_provider, sizeof(s_provider), provider_canonical_name(MIMI_SECRET_MODEL_PROVIDER));
    }

    /* NVS overrides take highest priority (set via CLI) */
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_LLM, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[LLM_API_KEY_MAX_LEN] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_API_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            safe_copy(s_api_key, sizeof(s_api_key), tmp);
        }
        char model_tmp[LLM_MODEL_MAX_LEN] = {0};
        len = sizeof(model_tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_MODEL, model_tmp, &len) == ESP_OK && model_tmp[0]) {
            safe_copy(s_model, sizeof(s_model), model_tmp);
        }
        char provider_tmp[LLM_PROVIDER_MAX_LEN] = {0};
        len = sizeof(provider_tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_PROVIDER, provider_tmp, &len) == ESP_OK && provider_tmp[0]) {
            safe_copy(s_provider, sizeof(s_provider), provider_canonical_name(provider_tmp));
        }
        nvs_close(nvs);
    }

    if (s_api_key[0]) {
        ESP_LOGI(TAG, "LLM proxy initialized (provider: %s, model: %s)", s_provider, s_model);
    } else {
        ESP_LOGW(TAG, "No default API key. Use CLI: set_api_key <KEY> or set_channel_api_key <channel> <KEY>");
    }
    return ESP_OK;
}

/* ── Direct path: esp_http_client ───────────────────────────── */

static esp_err_t llm_http_direct(const char *provider,
                                 const char *api_key,
                                 const char *post_data,
                                 resp_buf_t *rb,
                                 int *out_status)
{
    esp_http_client_config_t config = {
        .url = llm_api_url(provider),
        .event_handler = http_event_handler,
        .user_data = rb,
        .timeout_ms = 120 * 1000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (provider_is_openai_compatible(provider)) {
        if (api_key && api_key[0]) {
            char auth[LLM_API_KEY_MAX_LEN + 16];
            snprintf(auth, sizeof(auth), "Bearer %s", api_key);
            esp_http_client_set_header(client, "Authorization", auth);
        }
    } else {
        esp_http_client_set_header(client, "x-api-key", api_key ? api_key : "");
        esp_http_client_set_header(client, "anthropic-version", MIMI_LLM_API_VERSION);
    }
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

/* ── Proxy path: manual HTTP over CONNECT tunnel ────────────── */

static esp_err_t llm_http_via_proxy(const char *provider,
                                    const char *api_key,
                                    const char *post_data,
                                    resp_buf_t *rb,
                                    int *out_status)
{
    proxy_conn_t *conn = proxy_conn_open(llm_api_host(provider), 443, 30000);
    if (!conn) return ESP_ERR_HTTP_CONNECT;

    int body_len = strlen(post_data);
    char header[1024];
    int hlen = 0;
    if (provider_is_openai_compatible(provider)) {
        hlen = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            llm_api_path(provider), llm_api_host(provider), api_key ? api_key : "", body_len);
    } else {
        hlen = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "x-api-key: %s\r\n"
            "anthropic-version: %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n",
            llm_api_path(provider), llm_api_host(provider), api_key ? api_key : "", MIMI_LLM_API_VERSION, body_len);
    }

    if (proxy_conn_write(conn, header, hlen) < 0 ||
        proxy_conn_write(conn, post_data, body_len) < 0) {
        proxy_conn_close(conn);
        return ESP_ERR_HTTP_WRITE_DATA;
    }

    /* Read full response into buffer */
    char tmp[4096];
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 120000);
        if (n <= 0) break;
        if (resp_buf_append(rb, tmp, n) != ESP_OK) break;
    }
    proxy_conn_close(conn);

    /* Parse status line */
    *out_status = 0;
    if (rb->len > 5 && strncmp(rb->data, "HTTP/", 5) == 0) {
        const char *sp = strchr(rb->data, ' ');
        if (sp) *out_status = atoi(sp + 1);
    }

    /* Strip HTTP headers, keep body only */
    char *body = strstr(rb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = rb->len - (body - rb->data);
        memmove(rb->data, body, blen);
        rb->len = blen;
        rb->data[rb->len] = '\0';
    }

    /* Decode chunked transfer encoding if present */
    resp_buf_decode_chunked(rb);

    return ESP_OK;
}

/* ── Shared HTTP dispatch ─────────────────────────────────────── */

static esp_err_t llm_http_call(const char *provider,
                               const char *api_key,
                               const char *post_data,
                               resp_buf_t *rb,
                               int *out_status)
{
    if (http_proxy_is_enabled()) {
        return llm_http_via_proxy(provider, api_key, post_data, rb, out_status);
    } else {
        return llm_http_direct(provider, api_key, post_data, rb, out_status);
    }
}

static cJSON *convert_tools_openai(const char *tools_json)
{
    if (!tools_json) return NULL;
    cJSON *arr = cJSON_Parse(tools_json);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return NULL;
    }
    cJSON *out = cJSON_CreateArray();
    cJSON *tool;
    cJSON_ArrayForEach(tool, arr) {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        cJSON *desc = cJSON_GetObjectItem(tool, "description");
        cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");
        if (!name || !cJSON_IsString(name)) continue;

        cJSON *func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", name->valuestring);
        if (desc && cJSON_IsString(desc)) {
            cJSON_AddStringToObject(func, "description", desc->valuestring);
        }
        if (schema) {
            cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(schema, 1));
        }

        cJSON *wrap = cJSON_CreateObject();
        cJSON_AddStringToObject(wrap, "type", "function");
        cJSON_AddItemToObject(wrap, "function", func);
        cJSON_AddItemToArray(out, wrap);
    }
    cJSON_Delete(arr);
    return out;
}

static cJSON *convert_messages_openai(const char *system_prompt, cJSON *messages)
{
    cJSON *out = cJSON_CreateArray();
    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(out, sys);
    }

    if (!messages || !cJSON_IsArray(messages)) return out;

    cJSON *msg;
    cJSON_ArrayForEach(msg, messages) {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !cJSON_IsString(role)) continue;

        if (content && cJSON_IsString(content)) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", role->valuestring);
            cJSON_AddStringToObject(m, "content", content->valuestring);
            cJSON_AddItemToArray(out, m);
            continue;
        }

        if (!content || !cJSON_IsArray(content)) continue;

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", "assistant");

            /* collect text */
            char *text_buf = NULL;
            size_t off = 0;
            cJSON *block;
            cJSON *tool_calls = NULL;
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        size_t tlen = strlen(text->valuestring);
                        char *tmp = realloc(text_buf, off + tlen + 1);
                        if (tmp) {
                            text_buf = tmp;
                            memcpy(text_buf + off, text->valuestring, tlen);
                            off += tlen;
                            text_buf[off] = '\0';
                        }
                    }
                } else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "tool_use") == 0) {
                    if (!tool_calls) tool_calls = cJSON_CreateArray();
                    cJSON *id = cJSON_GetObjectItem(block, "id");
                    cJSON *name = cJSON_GetObjectItem(block, "name");
                    cJSON *input = cJSON_GetObjectItem(block, "input");
                    if (!name || !cJSON_IsString(name)) continue;

                    cJSON *tc = cJSON_CreateObject();
                    if (id && cJSON_IsString(id)) {
                        cJSON_AddStringToObject(tc, "id", id->valuestring);
                    }
                    cJSON_AddStringToObject(tc, "type", "function");
                    cJSON *func = cJSON_CreateObject();
                    cJSON_AddStringToObject(func, "name", name->valuestring);
                    if (input) {
                        char *args = cJSON_PrintUnformatted(input);
                        if (args) {
                            cJSON_AddStringToObject(func, "arguments", args);
                            free(args);
                        }
                    }
                    cJSON_AddItemToObject(tc, "function", func);
                    cJSON_AddItemToArray(tool_calls, tc);
                }
            }
            if (text_buf) {
                cJSON_AddStringToObject(m, "content", text_buf);
            } else {
                cJSON_AddStringToObject(m, "content", "");
            }
            if (tool_calls) {
                cJSON_AddItemToObject(m, "tool_calls", tool_calls);
            }
            cJSON_AddItemToArray(out, m);
            free(text_buf);
        } else if (strcmp(role->valuestring, "user") == 0) {
            /* tool_result blocks become role=tool */
            cJSON *block;
            bool has_user_text = false;
            char *text_buf = NULL;
            size_t off = 0;
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "tool_result") == 0) {
                    cJSON *tool_id = cJSON_GetObjectItem(block, "tool_use_id");
                    cJSON *tcontent = cJSON_GetObjectItem(block, "content");
                    if (!tool_id || !cJSON_IsString(tool_id)) continue;
                    cJSON *tm = cJSON_CreateObject();
                    cJSON_AddStringToObject(tm, "role", "tool");
                    cJSON_AddStringToObject(tm, "tool_call_id", tool_id->valuestring);
                    if (tcontent && cJSON_IsString(tcontent)) {
                        cJSON_AddStringToObject(tm, "content", tcontent->valuestring);
                    } else {
                        cJSON_AddStringToObject(tm, "content", "");
                    }
                    cJSON_AddItemToArray(out, tm);
                } else if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        size_t tlen = strlen(text->valuestring);
                        char *tmp = realloc(text_buf, off + tlen + 1);
                        if (tmp) {
                            text_buf = tmp;
                            memcpy(text_buf + off, text->valuestring, tlen);
                            off += tlen;
                            text_buf[off] = '\0';
                        }
                        has_user_text = true;
                    }
                }
            }
            if (has_user_text) {
                cJSON *um = cJSON_CreateObject();
                cJSON_AddStringToObject(um, "role", "user");
                cJSON_AddStringToObject(um, "content", text_buf);
                cJSON_AddItemToArray(out, um);
            }
            free(text_buf);
        }
    }

    return out;
}

/* ── Public: chat with tools (non-streaming) ──────────────────── */

void llm_response_free(llm_response_t *resp)
{
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

esp_err_t llm_chat_tools_for_channel(const char *channel,
                                     const char *system_prompt,
                                     cJSON *messages,
                                     const char *tools_json,
                                     llm_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));

    llm_runtime_config_t effective = {0};
    llm_get_effective_config_for_channel(channel, &effective);

    if (effective.api_key[0] == '\0') return ESP_ERR_INVALID_STATE;

    /* Build request body (non-streaming) */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", effective.model);
    if (provider_is_openai_compatible(effective.provider)) {
        if (provider_is_minimax(effective.provider) || provider_is_volcengine(effective.provider)) {
            cJSON_AddNumberToObject(body, "max_tokens", MIMI_LLM_MAX_TOKENS);
        } else {
            cJSON_AddNumberToObject(body, "max_completion_tokens", MIMI_LLM_MAX_TOKENS);
        }
    } else {
        cJSON_AddNumberToObject(body, "max_tokens", MIMI_LLM_MAX_TOKENS);
    }

    if (provider_is_openai_compatible(effective.provider)) {
        cJSON *openai_msgs = convert_messages_openai(system_prompt, messages);
        cJSON_AddItemToObject(body, "messages", openai_msgs);

        if (tools_json) {
            cJSON *tools = convert_tools_openai(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
                cJSON_AddStringToObject(body, "tool_choice", "auto");
            }
        }
    } else {
        cJSON_AddStringToObject(body, "system", system_prompt);

        /* Deep-copy messages so caller keeps ownership */
        cJSON *msgs_copy = cJSON_Duplicate(messages, 1);
        cJSON_AddItemToObject(body, "messages", msgs_copy);

        /* Add tools array if provided */
        if (tools_json) {
            cJSON *tools = cJSON_Parse(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
            }
        }
    }

    char *post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!post_data) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Calling LLM API with tools (channel: %s, provider: %s, model: %s, body: %d bytes)",
             channel ? channel : "(default)", effective.provider, effective.model, (int)strlen(post_data));
    llm_log_payload("LLM tools request", post_data);

    /* HTTP call */
    resp_buf_t rb;
    if (resp_buf_init(&rb, MIMI_LLM_STREAM_BUF_SIZE) != ESP_OK) {
        free(post_data);
        return ESP_ERR_NO_MEM;
    }

    int status = 0;
    esp_err_t err = llm_http_call(effective.provider, effective.api_key, post_data, &rb, &status);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        llm_log_payload("LLM tools partial response", rb.data);
        resp_buf_free(&rb);
        return err;
    }

    llm_log_payload("LLM tools raw response", rb.data);

    if (status != 200) {
        ESP_LOGE(TAG, "API error %d: %.500s", status, rb.data ? rb.data : "");
        resp_buf_free(&rb);
        return ESP_FAIL;
    }

    /* Parse full JSON response */
    cJSON *root = cJSON_Parse(rb.data);
    resp_buf_free(&rb);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse API response JSON");
        return ESP_FAIL;
    }

    if (provider_is_openai_compatible(effective.provider)) {
        cJSON *choices = cJSON_GetObjectItem(root, "choices");
        cJSON *choice0 = choices && cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        if (choice0) {
            cJSON *finish = cJSON_GetObjectItem(choice0, "finish_reason");
            if (finish && cJSON_IsString(finish)) {
                resp->tool_use = (strcmp(finish->valuestring, "tool_calls") == 0);
            }

            cJSON *message = cJSON_GetObjectItem(choice0, "message");
            if (message) {
                cJSON *content = cJSON_GetObjectItem(message, "content");
                if (content && cJSON_IsString(content)) {
                    size_t tlen = strlen(content->valuestring);
                    resp->text = calloc(1, tlen + 1);
                    if (resp->text) {
                        memcpy(resp->text, content->valuestring, tlen);
                        resp->text_len = tlen;
                    }
                }

                cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    cJSON *tc;
                    cJSON_ArrayForEach(tc, tool_calls) {
                        if (resp->call_count >= MIMI_MAX_TOOL_CALLS) break;
                        llm_tool_call_t *call = &resp->calls[resp->call_count];
                        cJSON *id = cJSON_GetObjectItem(tc, "id");
                        cJSON *func = cJSON_GetObjectItem(tc, "function");
                        if (id && cJSON_IsString(id)) {
                            strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
                        }
                        if (func) {
                            cJSON *name = cJSON_GetObjectItem(func, "name");
                            cJSON *args = cJSON_GetObjectItem(func, "arguments");
                            if (name && cJSON_IsString(name)) {
                                strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                            }
                            if (args && cJSON_IsString(args)) {
                                call->input = strdup(args->valuestring);
                                if (call->input) {
                                    call->input_len = strlen(call->input);
                                }
                            }
                        }
                        resp->call_count++;
                    }
                    if (resp->call_count > 0) {
                        resp->tool_use = true;
                    }
                }
            }
        }
    } else {
        /* stop_reason */
        cJSON *stop_reason = cJSON_GetObjectItem(root, "stop_reason");
        if (stop_reason && cJSON_IsString(stop_reason)) {
            resp->tool_use = (strcmp(stop_reason->valuestring, "tool_use") == 0);
        }

        /* Iterate content blocks */
        cJSON *content = cJSON_GetObjectItem(root, "content");
        if (content && cJSON_IsArray(content)) {
            /* Accumulate total text length first */
            size_t total_text = 0;
            cJSON *block;
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        total_text += strlen(text->valuestring);
                    }
                }
            }

            /* Allocate and copy text */
            if (total_text > 0) {
                resp->text = calloc(1, total_text + 1);
                if (resp->text) {
                    cJSON_ArrayForEach(block, content) {
                        cJSON *btype = cJSON_GetObjectItem(block, "type");
                        if (!btype || strcmp(btype->valuestring, "text") != 0) continue;
                        cJSON *text = cJSON_GetObjectItem(block, "text");
                        if (!text || !cJSON_IsString(text)) continue;
                        size_t tlen = strlen(text->valuestring);
                        memcpy(resp->text + resp->text_len, text->valuestring, tlen);
                        resp->text_len += tlen;
                    }
                    resp->text[resp->text_len] = '\0';
                }
            }

            /* Extract tool_use blocks */
            cJSON_ArrayForEach(block, content) {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (!btype || strcmp(btype->valuestring, "tool_use") != 0) continue;
                if (resp->call_count >= MIMI_MAX_TOOL_CALLS) break;

                llm_tool_call_t *call = &resp->calls[resp->call_count];

                cJSON *id = cJSON_GetObjectItem(block, "id");
                if (id && cJSON_IsString(id)) {
                    strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
                }

                cJSON *name = cJSON_GetObjectItem(block, "name");
                if (name && cJSON_IsString(name)) {
                    strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                }

                cJSON *input = cJSON_GetObjectItem(block, "input");
                if (input) {
                    char *input_str = cJSON_PrintUnformatted(input);
                    if (input_str) {
                        call->input = input_str;
                        call->input_len = strlen(input_str);
                    }
                }

                resp->call_count++;
            }
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Response: %d bytes text, %d tool calls, stop=%s",
             (int)resp->text_len, resp->call_count,
             resp->tool_use ? "tool_use" : "end_turn");

    return ESP_OK;
}

/* ── NVS helpers ──────────────────────────────────────────────── */

esp_err_t llm_set_api_key(const char *api_key)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_API_KEY, api_key));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    safe_copy(s_api_key, sizeof(s_api_key), api_key);
    ESP_LOGI(TAG, "API key saved");
    return ESP_OK;
}

esp_err_t llm_set_channel_api_key(const char *channel, const char *api_key)
{
    const channel_llm_config_t *cfg = channel_llm_config_for(channel);
    if (!cfg) {
        ESP_LOGW(TAG, "Unknown channel for API key override: %s", channel ? channel : "(null)");
        return ESP_ERR_INVALID_ARG;
    }

    bool clear_override = false;
    if (!api_key || api_key[0] == '\0' || strcmp(api_key, "default") == 0) {
        clear_override = true;
    }

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs));
    if (clear_override) {
        esp_err_t err = nvs_erase_key(nvs, cfg->api_key_nvs_key);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(nvs);
            return err;
        }
    } else {
        ESP_ERROR_CHECK(nvs_set_str(nvs, cfg->api_key_nvs_key, api_key));
    }
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    ESP_LOGI(TAG, "Channel API key %s for %s",
             clear_override ? "cleared" : "saved",
             channel);
    return ESP_OK;
}

esp_err_t llm_set_model(const char *model)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_MODEL, model));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    safe_copy(s_model, sizeof(s_model), model);
    ESP_LOGI(TAG, "Model set to: %s", s_model);
    return ESP_OK;
}

esp_err_t llm_set_channel_model(const char *channel, const char *model)
{
    const channel_llm_config_t *cfg = channel_llm_config_for(channel);
    if (!cfg) {
        ESP_LOGW(TAG, "Unknown channel for model override: %s", channel ? channel : "(null)");
        return ESP_ERR_INVALID_ARG;
    }

    bool clear_override = false;
    if (!model || model[0] == '\0' || strcmp(model, "default") == 0) {
        clear_override = true;
    }

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs));
    if (clear_override) {
        esp_err_t err = nvs_erase_key(nvs, cfg->model_nvs_key);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(nvs);
            return err;
        }
    } else {
        ESP_ERROR_CHECK(nvs_set_str(nvs, cfg->model_nvs_key, model));
    }
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    ESP_LOGI(TAG, "Channel model %s for %s",
             clear_override ? "cleared" : "saved",
             channel);
    return ESP_OK;
}

esp_err_t llm_set_provider(const char *provider)
{
    const char *canonical = provider_canonical_name(provider);

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_PROVIDER, canonical));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    safe_copy(s_provider, sizeof(s_provider), canonical);
    ESP_LOGI(TAG, "Provider set to: %s", s_provider);
    return ESP_OK;
}

esp_err_t llm_set_channel_provider(const char *channel, const char *provider)
{
    const channel_llm_config_t *cfg = channel_llm_config_for(channel);
    if (!cfg) {
        ESP_LOGW(TAG, "Unknown channel for provider override: %s", channel ? channel : "(null)");
        return ESP_ERR_INVALID_ARG;
    }

    bool clear_override = false;
    if (!provider || provider[0] == '\0' || strcmp(provider, "default") == 0) {
        clear_override = true;
    }

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_LLM, NVS_READWRITE, &nvs));
    if (clear_override) {
        esp_err_t err = nvs_erase_key(nvs, cfg->provider_nvs_key);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(nvs);
            return err;
        }
    } else {
        const char *canonical = provider_canonical_name(provider);
        ESP_ERROR_CHECK(nvs_set_str(nvs, cfg->provider_nvs_key, canonical));
    }
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    ESP_LOGI(TAG, "Channel provider %s for %s",
             clear_override ? "cleared" : "saved",
             channel);
    return ESP_OK;
}

esp_err_t llm_chat_tools(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp)
{
    return llm_chat_tools_for_channel(NULL, system_prompt, messages, tools_json, resp);
}
