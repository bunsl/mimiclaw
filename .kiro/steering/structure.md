---
inclusion: auto
---

# 项目结构

## 顶层目录

```
mimiclaw/
├── main/              # 主应用代码（所有 C 源文件）
├── docs/              # 技术文档
├── assets/            # 图片和媒体资源
├── spiffs_data/       # SPIFFS 预加载数据（引导文件）
├── scripts/           # 构建和设置脚本
├── managed_components/# ESP-IDF 管理的组件
├── CMakeLists.txt     # 顶层 CMake 配置
└── partitions.csv     # Flash 分区表
```

## main/ 目录结构

按功能模块组织，每个模块包含 `.h` 和 `.c` 文件：

```
main/
├── mimi.c                    # 入口点：app_main()
├── mimi_config.h             # 全局配置常量
├── mimi_secrets.h            # 构建时凭证（gitignored）
├── mimi_secrets.h.example    # 凭证模板
│
├── agent/                    # AI 代理核心
│   ├── agent_loop.c/h        # ReAct 循环：LLM 调用 → 工具执行
│   └── context_builder.c/h   # 系统提示词和消息构建器
│
├── bus/                      # 消息总线
│   └── message_bus.c/h       # FreeRTOS 队列：入站/出站
│
├── channels/                 # 通信通道
│   ├── telegram/             # Telegram Bot API
│   │   ├── telegram_bot.c/h
│   │   └── README.md
│   └── feishu/               # 飞书 Bot API
│       ├── feishu_bot.c/h
│       └── README.md
│
├── llm/                      # LLM 提供商集成
│   └── llm_proxy.c/h         # Anthropic/OpenAI API 客户端
│
├── tools/                    # 工具注册和实现
│   ├── tool_registry.c/h     # 工具注册、JSON schema、分发
│   ├── tool_web_search.c/h   # 网络搜索（Tavily/Brave）
│   ├── tool_get_time.c/h     # 获取当前时间
│   ├── tool_cron.c/h         # Cron 任务管理
│   ├── tool_files.c/h        # 文件操作
│   └── tool_gpio.c/h         # GPIO 控制
│
├── memory/                   # 持久化存储
│   ├── memory_store.c/h      # MEMORY.md、每日笔记
│   └── session_mgr.c/h       # 每个聊天的 JSONL 会话
│
├── wifi/                     # WiFi 管理
│   └── wifi_manager.c/h      # STA 模式、重连逻辑
│
├── gateway/                  # WebSocket 网关
│   └── ws_server.c/h         # 端口 18789 上的 WS 服务器
│
├── proxy/                    # HTTP 代理
│   └── http_proxy.c/h        # CONNECT 隧道支持
│
├── cli/                      # 串行命令行
│   └── serial_cli.c/h        # esp_console REPL
│
├── cron/                     # 定时任务调度器
│   └── cron_service.c/h      # 周期性和一次性任务
│
├── heartbeat/                # 自主任务执行
│   └── heartbeat.c/h         # 定期检查 HEARTBEAT.md
│
├── skills/                   # 技能加载器
│   └── skill_loader.c/h      # 从 SPIFFS 加载技能
│
├── onboard/                  # WiFi 配置门户
│   ├── wifi_onboard.c/h      # 强制门户 AP 模式
│   └── onboard_html.h        # 嵌入式 HTML
│
└── ota/                      # 空中升级
    └── ota_manager.c/h       # 通过 WiFi 更新固件
```

## 模块职责

### 核心系统
- **mimi.c**: 应用入口，初始化所有子系统，启动任务
- **message_bus**: 通道和代理循环之间的消息路由
- **agent_loop**: 主 AI 循环（Core 1），处理消息并调用 LLM

### 通信层
- **channels/**: 外部接口（Telegram、飞书、WebSocket）
- **gateway/**: 局域网 WebSocket 服务器
- **wifi/**: 网络连接管理

### AI 层
- **llm/**: LLM API 客户端（Anthropic、OpenAI）
- **tools/**: 可执行工具（网络搜索、时间、cron、文件、GPIO）
- **context_builder**: 组装系统提示词和对话历史

### 存储层
- **memory/**: 长期记忆和会话历史（SPIFFS）
- **skills/**: 从 flash 加载技能定义

### 自动化
- **cron/**: AI 创建的定时任务
- **heartbeat/**: 定期任务检查和执行

### 实用工具
- **cli/**: 调试和配置命令
- **proxy/**: 受限网络的 HTTP 代理
- **ota/**: 无线固件更新
- **onboard/**: WiFi 设置强制门户

## 文件命名约定

- 头文件：`module_name.h`
- 实现文件：`module_name.c`
- 配置：`mimi_config.h`（全局）、`mimi_secrets.h`（凭证）
- 常量前缀：`MIMI_` 用于所有 #define
- 函数前缀：`module_name_` 用于公共 API

## 代码组织原则

1. **模块化**：每个功能一个目录，清晰的 API 边界
2. **头文件**：最小化公共 API，内部函数使用 static
3. **错误处理**：所有公共函数返回 `esp_err_t`
4. **内存管理**：
   - 小缓冲区（< 32 KB）：内部 SRAM
   - 大缓冲区（≥ 32 KB）：PSRAM via `heap_caps_calloc`
   - 明确的所有权转移（例如 message_bus 内容）
5. **日志**：每个模块使用静态 TAG，使用 ESP_LOG* 宏
6. **配置**：所有常量在 `mimi_config.h` 中，凭证在 `mimi_secrets.h` 中

## SPIFFS 文件布局

```
/spiffs/
├── config/
│   ├── SOUL.md           # AI 个性定义
│   └── USER.md           # 用户配置文件
├── memory/
│   ├── MEMORY.md         # 长期记忆
│   └── YYYY-MM-DD.md     # 每日笔记
├── sessions/
│   └── tg_<chat_id>.jsonl  # 每个聊天的历史
├── skills/
│   └── <skill_name>.md   # 技能定义
├── HEARTBEAT.md          # 自主任务列表
└── cron.json             # 定时任务配置
```

## 任务分配（FreeRTOS）

| 任务 | 核心 | 优先级 | 栈大小 | 模块 |
|------|------|--------|--------|------|
| agent_loop | 1 | 6 | 24 KB | agent/ |
| tg_poll | 0 | 5 | 12 KB | channels/telegram/ |
| outbound | 0 | 5 | 12 KB | mimi.c |
| serial_cli | 0 | 3 | 4 KB | cli/ |
| httpd | 0 | 5 | - | gateway/ |

**核心分配策略**：
- Core 0：I/O（网络、串行、WiFi）
- Core 1：AI 处理（代理循环、LLM 调用）

## 依赖关系

```
mimi.c
  ├─→ message_bus (所有模块使用)
  ├─→ wifi_manager
  ├─→ channels/* → message_bus
  ├─→ agent_loop → llm_proxy
  │              → tools/*
  │              → memory/*
  │              → context_builder
  ├─→ gateway → message_bus
  ├─→ cron_service → message_bus
  └─→ heartbeat → message_bus
```

## 添加新模块

1. 在 `main/` 下创建目录
2. 添加 `module_name.h`（公共 API）和 `module_name.c`（实现）
3. 在 `main/CMakeLists.txt` 中添加源文件
4. 在 `mimi_config.h` 中添加配置常量
5. 在 `mimi.c` 的 `app_main()` 中初始化
6. 如果需要任务，使用 `xTaskCreatePinnedToCore` 并选择适当的核心

## 添加新工具

1. 在 `main/tools/` 中创建 `tool_<name>.c/h`
2. 实现工具函数：`esp_err_t tool_<name>_execute(cJSON *input, char **output)`
3. 在 `tool_registry.c` 的 `tool_registry_init()` 中注册
4. 定义 JSON schema（名称、描述、input_schema）
5. 工具将自动包含在 LLM 请求中
