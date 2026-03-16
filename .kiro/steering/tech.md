---
inclusion: auto
---

# 技术栈

## 构建系统

- **ESP-IDF v5.5+**：Espressif 物联网开发框架
- **CMake 3.16+**：构建配置管理
- **Ninja**：快速构建工具
- **Python 3.10+**：ESP-IDF 工具链依赖

## 核心技术

- **编程语言**：纯 C（C11 标准）
- **操作系统**：FreeRTOS（双核任务调度）
- **目标芯片**：ESP32-S3（Xtensa LX7 双核 @ 240 MHz）
- **Flash 存储**：16 MB（分区用于 OTA、SPIFFS、coredump）
- **内存**：512 KB 内部 SRAM + 8 MB PSRAM

## 关键库和组件

### ESP-IDF 组件
安装目录：D:\Espressif
- **esp_http_client**：HTTP/HTTPS 请求（LLM API、Telegram、网络搜索）
- **esp_tls**：TLS/SSL 安全连接
- **esp_http_server**：WebSocket 服务器网关
- **esp_spiffs**：Flash 文件系统
- **nvs_flash**：非易失性存储（运行时配置）
- **esp_console**：串口 CLI/REPL

### 第三方组件
- **cJSON**：JSON 解析和生成
- **esp_websocket_client**：WebSocket 客户端（管理组件）

### 网络协议
- WiFi STA 模式（带指数退避重连）
- HTTPS（TLS 1.2+）用于所有外部 API
- HTTP CONNECT 代理支持
- WebSocket（RFC 6455）用于局域网网关
- Telegram Bot API（长轮询）
- 飞书 Bot API（webhook）

### 外部 API
- Anthropic Messages API（Claude）
- OpenAI Chat Completions API（GPT）
- Tavily Search API（推荐）
- Brave Search API（备选）

## 常用命令

### 初始设置

```bash
# 安装 ESP-IDF v5.5+
# https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/

# 克隆并配置项目
git clone https://github.com/memovai/mimiclaw.git
cd mimiclaw
idf.py set-target esp32s3

# 复制并编辑密钥文件
cp main/mimi_secrets.h.example main/mimi_secrets.h
# 编辑 main/mimi_secrets.h 填入凭证
```

### 构建和烧录

```bash
# 完整清理构建（修改 mimi_secrets.h 后必须执行）
idf.py fullclean && idf.py build

# 增量构建
idf.py build

# 烧录固件（替换 PORT 为实际串口）
idf.py -p PORT flash

# 烧录并监控
idf.py -p PORT flash monitor

# 仅监控日志
idf.py -p PORT monitor
```

### 开发工具

```bash
# 清理构建产物
idf.py clean

# 完全清理（包括配置）
idf.py fullclean

# 打开配置菜单
idf.py menuconfig

# 查看分区表
idf.py partition-table

# 完全擦除 Flash
idf.py -p PORT erase-flash
```

### 端口检测

```bash
# macOS
ls /dev/cu.usb*

# Linux
ls /dev/ttyACM* /dev/ttyUSB*
```

## 内存管理

### 内存分配策略

- **内部 SRAM**：任务栈、WiFi 缓冲区（约 70 KB 已使用）
- **PSRAM**：大缓冲区（≥ 32 KB）通过 `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)` 分配
- **SPIFFS**：12 MB Flash 分区用于文件存储
- **NVS**：24 KB 用于运行时配置覆盖

### 内存使用指南

```c
// 小缓冲区（< 32 KB）：使用栈或普通 malloc
char buffer[256];
char *msg = malloc(1024);

// 大缓冲区（≥ 32 KB）：使用 PSRAM
char *large_buf = heap_caps_calloc(1, 64 * 1024, MALLOC_CAP_SPIRAM);

// 检查内存使用
ESP_LOGI(TAG, "Internal free: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
ESP_LOGI(TAG, "PSRAM free: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

### 内存预算

| 用途 | 位置 | 大小 |
|------|------|------|
| FreeRTOS 任务栈 | 内部 SRAM | ~40 KB |
| WiFi 缓冲区 | 内部 SRAM | ~30 KB |
| TLS 连接 x2 | PSRAM | ~120 KB |
| JSON 解析缓冲区 | PSRAM | ~32 KB |
| 会话历史缓存 | PSRAM | ~32 KB |
| 系统提示词缓冲区 | PSRAM | ~16 KB |
| LLM 响应缓冲区 | PSRAM | ~32 KB |
| 剩余可用 | PSRAM | ~7.7 MB |

## 配置系统

### 两层配置架构

MimiClaw 使用两层配置系统，优先级从高到低：

1. **构建时配置**：`main/mimi_secrets.h`（gitignored，最高优先级）
2. **运行时配置**：NVS Flash（通过串口 CLI 设置，覆盖构建时默认值）

### 构建时配置

在 `main/mimi_secrets.h` 中定义：

```c
#define MIMI_SECRET_WIFI_SSID       "YourWiFiName"
#define MIMI_SECRET_WIFI_PASS       "YourWiFiPassword"
#define MIMI_SECRET_TG_TOKEN        "123456:ABC-DEF..."
#define MIMI_SECRET_API_KEY         "sk-ant-api03-xxxxx"
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"  // "anthropic" 或 "openai"
#define MIMI_SECRET_SEARCH_KEY      ""           // 可选：Brave Search API key
#define MIMI_SECRET_TAVILY_KEY      ""           // 可选：Tavily API key（推荐）
#define MIMI_SECRET_PROXY_HOST      ""           // 可选：代理主机
#define MIMI_SECRET_PROXY_PORT      ""           // 可选：代理端口
```

**注意**：修改 `mimi_secrets.h` 后必须执行 `idf.py fullclean && idf.py build`

### 运行时配置

通过串口 CLI 修改（无需重新编译）：

```bash
# 连接到 UART 端口
idf.py -p /dev/cu.usbserial-110 monitor

# 在 CLI 中执行配置命令
mimi> wifi_set MySSID MyPassword
mimi> set_tg_token 123456:ABC...
mimi> set_api_key sk-ant-api03-...
mimi> set_model_provider openai
mimi> set_model gpt-4o
mimi> set_proxy 127.0.0.1 7897
mimi> clear_proxy
mimi> set_search_key BSA...
mimi> set_tavily_key tvly-...
mimi> config_show              # 显示所有配置（敏感信息已屏蔽）
mimi> config_reset             # 清除 NVS，恢复构建时默认值
mimi> restart                  # 重启设备
```

### 配置常量

所有系统常量定义在 `main/mimi_config.h`：

```c
// WiFi
#define MIMI_WIFI_MAX_RETRY          10
#define MIMI_WIFI_RETRY_BASE_MS      1000

// Telegram Bot
#define MIMI_TG_POLL_TIMEOUT_S       30
#define MIMI_TG_MAX_MSG_LEN          4096

// Agent Loop
#define MIMI_AGENT_MAX_HISTORY       20
#define MIMI_AGENT_MAX_TOOL_ITER     10

// LLM
#define MIMI_LLM_DEFAULT_MODEL       "claude-opus-4-5"
#define MIMI_LLM_MAX_TOKENS          4096

// Memory / SPIFFS
#define MIMI_SPIFFS_BASE             "/spiffs"
#define MIMI_SESSION_MAX_MSGS        20

// Cron / Heartbeat
#define MIMI_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define MIMI_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

// WebSocket Gateway
#define MIMI_WS_PORT                 18789
#define MIMI_WS_MAX_CLIENTS          4
```

## 日志系统

ESP-IDF 日志系统，支持多级别日志：

```c
// 每个模块定义静态 TAG
static const char *TAG = "module_name";

// 日志级别
ESP_LOGD(TAG, "Debug message");    // 调试（默认禁用）
ESP_LOGI(TAG, "Info message");     // 信息
ESP_LOGW(TAG, "Warning message");  // 警告
ESP_LOGE(TAG, "Error message");    // 错误

// 动态调整日志级别
esp_log_level_set("wifi_manager", ESP_LOG_DEBUG);
esp_log_level_set("*", ESP_LOG_INFO);  // 所有模块
```

## Flash 分区布局

```
偏移地址    大小      名称        用途
─────────────────────────────────────────────
0x009000    24 KB     nvs         ESP-IDF 内部使用（WiFi 校准等）
0x00F000     8 KB     otadata     OTA 启动状态
0x011000     4 KB     phy_init    WiFi PHY 校准
0x020000     2 MB     ota_0       固件槽 A
0x220000     2 MB     ota_1       固件槽 B
0x420000    12 MB     spiffs      Markdown 记忆、会话、配置
0xFF0000    64 KB     coredump    崩溃转储存储
```

总计：16 MB Flash

## 开发环境

### Ubuntu 依赖

```bash
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
  libusb-1.0-0
```

### macOS 依赖

```bash
xcode-select --install
brew install cmake ninja dfu-util python@3.10
```

### ESP-IDF 安装

参考官方文档：
https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/
