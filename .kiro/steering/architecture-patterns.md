---
inclusion: auto
---

# 架构模式和设计原则

## 核心架构模式

### 消息总线模式

MimiClaw 使用中央消息总线进行模块间通信：

```
通道 (Telegram/WS) → 入站队列 → 代理循环 → 出站队列 → 分发器 → 通道
```

**关键点**：
- 所有通信通过 FreeRTOS 队列
- 消息使用 `mimi_msg_t` 结构体
- 内容所有权在 push 时转移
- 接收方负责释放内存

**实现示例**：
```c
// 发送消息到入站队列
mimi_msg_t msg = {
    .channel = "telegram",
    .chat_id = "12345",
    .content = strdup("Hello")  // 分配新内存
};
message_bus_push_inbound(&msg);

// 接收方必须释放
mimi_msg_t received;
message_bus_pop_inbound(&received, portMAX_DELAY);
// 处理消息...
free(received.content);  // 释放内存
```

### ReAct 代理模式

代理循环实现 ReAct（Reasoning + Acting）模式：

```
1. 接收用户消息
2. 构建上下文（系统提示词 + 历史 + 工具定义）
3. 调用 LLM
4. 解析响应：
   - 如果是文本 → 返回给用户
   - 如果是 tool_use → 执行工具 → 回到步骤 3
5. 保存对话历史
```

**最大迭代次数**：10 次（防止无限循环）

**工具执行流程**：
```c
// LLM 返回 tool_use
if (stop_reason == "tool_use") {
    for (each tool_use block) {
        // 1. 从注册表查找工具
        tool_func = tool_registry_get(tool_name);
        
        // 2. 执行工具
        char *result = NULL;
        tool_func(input_json, &result);
        
        // 3. 构建 tool_result
        append_tool_result(messages, tool_use_id, result);
        free(result);
    }
    
    // 4. 继续循环，将 tool_result 发送回 LLM
    continue;
}
```

### 双核任务分配

ESP32-S3 有两个 CPU 核心，任务分配策略：

**Core 0 (I/O 核心)**：
- Telegram 轮询
- WebSocket 服务器
- 串口 CLI
- 出站消息分发
- WiFi 事件处理

**Core 1 (计算核心)**：
- 代理循环（AI 处理）
- LLM API 调用
- JSON 解析和构建
- 上下文构建

**原因**：
- 网络 I/O 不应阻塞 AI 处理
- AI 处理是 CPU 密集型（JSON 操作）
- 分离关注点，提高响应性

### 分层存储架构

```
应用层
  ├─→ memory_store (MEMORY.md, 每日笔记)
  ├─→ session_mgr (会话历史 JSONL)
  └─→ skill_loader (技能定义)
       ↓
SPIFFS 层 (12 MB flash)
  ├─→ /spiffs/config/
  ├─→ /spiffs/memory/
  ├─→ /spiffs/sessions/
  └─→ /spiffs/skills/
```

**设计原则**：
- 所有持久化数据存储在 SPIFFS
- 使用纯文本格式（Markdown、JSON、JSONL）
- 人类可读可编辑
- 模块化访问接口

### 工具注册表模式

工具使用注册表模式实现动态调用：

```c
// 1. 定义工具结构
typedef struct {
    const char *name;
    const char *description;
    cJSON *input_schema;
    tool_execute_fn execute;
} tool_def_t;

// 2. 注册工具
void tool_registry_init(void) {
    tool_registry_register("web_search", 
                          "Search the web",
                          schema,
                          tool_web_search_execute);
}

// 3. 动态调用
tool_def_t *tool = tool_registry_get("web_search");
tool->execute(input, &output);
```

**优势**：
- 易于添加新工具
- LLM 自动获取工具列表
- 统一的调用接口

## 设计原则

### 1. 模块化和单一职责

每个模块负责一个明确的功能：
- `wifi_manager`: 仅处理 WiFi 连接
- `telegram_bot`: 仅处理 Telegram API
- `llm_proxy`: 仅处理 LLM API 调用

**避免**：
- 模块间直接调用（除了明确的依赖）
- 在一个模块中混合多个职责

### 2. 明确的所有权和生命周期

**内存所有权规则**：
- 函数返回的指针 → 调用者负责释放
- 传递给函数的指针 → 函数不应释放（除非明确说明）
- 队列传递的内容 → 接收方负责释放

**示例**：
```c
// 调用者必须释放返回的字符串
char *build_message(const char *input);

// 函数接管所有权，会释放 content
esp_err_t message_bus_push(mimi_msg_t *msg);

// 函数不会修改或释放 input
esp_err_t process_data(const char *input);
```

### 3. 错误处理和恢复

**错误处理策略**：
- 所有公共函数返回 `esp_err_t`
- 关键错误使用 `ESP_ERROR_CHECK`（会重启）
- 可恢复错误记录日志并返回错误码
- 网络错误实现重试机制

**示例**：
```c
esp_err_t wifi_manager_connect(void) {
    int retry = 0;
    while (retry < MIMI_WIFI_MAX_RETRY) {
        esp_err_t ret = esp_wifi_connect();
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        
        ESP_LOGW(TAG, "WiFi connect failed, retry %d/%d", 
                 retry + 1, MIMI_WIFI_MAX_RETRY);
        
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        retry++;
    }
    
    return ESP_FAIL;
}
```

### 4. 资源约束意识

**内存管理**：
- 小缓冲区（< 32 KB）：内部 SRAM
- 大缓冲区（≥ 32 KB）：PSRAM
- 避免内存碎片：使用固定大小的缓冲区池（如需要）

**栈大小**：
- 根据任务需求精确配置
- 使用 `uxTaskGetStackHighWaterMark()` 监控

**Flash 使用**：
- SPIFFS 限制为 12 MB
- 会话历史使用环形缓冲区（最多 20 条消息）
- 定期清理旧的每日笔记（如需要）

### 5. 异步和非阻塞

**网络操作**：
- 使用独立任务处理长时间操作
- Telegram 轮询使用 30 秒超时
- LLM API 调用在专用核心上

**任务间通信**：
- 使用队列而不是直接调用
- 避免在中断中执行复杂逻辑

### 6. 配置驱动

**两层配置系统**：
1. **构建时**：`mimi_secrets.h`（默认值）
2. **运行时**：NVS flash（覆盖默认值）

**优势**：
- 无需重新编译即可更改配置
- 支持现场部署和调试
- 敏感信息不进入版本控制

## 常见模式

### 初始化模式

```c
// 模块初始化函数
esp_err_t module_init(void) {
    // 1. 检查前置条件
    if (dependency_not_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 2. 分配资源
    module_state = calloc(1, sizeof(module_state_t));
    if (!module_state) {
        return ESP_ERR_NO_MEM;
    }
    
    // 3. 初始化状态
    module_state->initialized = true;
    
    // 4. 注册回调/事件处理器
    register_event_handler();
    
    ESP_LOGI(TAG, "Module initialized");
    return ESP_OK;
}
```

### 任务创建模式

```c
esp_err_t module_start(void) {
    BaseType_t ret = xTaskCreatePinnedToCore(
        module_task,           // 任务函数
        "module_task",         // 任务名称
        MODULE_STACK_SIZE,     // 栈大小（来自配置）
        NULL,                  // 参数
        MODULE_PRIORITY,       // 优先级（来自配置）
        &task_handle,          // 任务句柄（保存以便后续控制）
        MODULE_CORE            // CPU 核心（来自配置）
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}
```

### 事件处理模式

```c
static void event_handler(void *arg, esp_event_base_t base,
                         int32_t id, void *data) {
    switch (id) {
        case EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            on_connected();
            break;
            
        case EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected");
            on_disconnected();
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown event: %d", (int)id);
            break;
    }
}
```

## 反模式（避免）

### ❌ 全局可变状态

```c
// 不好：全局可变变量
static int connection_count = 0;

// 好：使用互斥锁保护或封装在模块内
static SemaphoreHandle_t state_mutex;
static int connection_count = 0;

int get_connection_count(void) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int count = connection_count;
    xSemaphoreGive(state_mutex);
    return count;
}
```

### ❌ 阻塞主循环

```c
// 不好：在任务中长时间阻塞
void task(void *arg) {
    while (1) {
        long_blocking_operation();  // 阻塞数秒
    }
}

// 好：使用超时或分解为小步骤
void task(void *arg) {
    while (1) {
        operation_with_timeout(1000);  // 1 秒超时
        vTaskDelay(pdMS_TO_TICKS(100));  // 让出 CPU
    }
}
```

### ❌ 忽略错误

```c
// 不好：忽略返回值
malloc(size);
esp_wifi_connect();

// 好：检查并处理错误
void *ptr = malloc(size);
if (!ptr) {
    ESP_LOGE(TAG, "Memory allocation failed");
    return ESP_ERR_NO_MEM;
}

esp_err_t ret = esp_wifi_connect();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    return ret;
}
```

## 扩展指南

### 添加新通道

1. 在 `main/channels/` 创建新目录
2. 实现初始化、启动、发送消息函数
3. 轮询或接收消息后推送到入站队列
4. 在出站分发器中添加路由逻辑
5. 在 `mimi.c` 中初始化和启动

### 添加新存储类型

1. 在 `main/memory/` 创建新模块
2. 定义文件路径常量（在 `mimi_config.h`）
3. 实现读写接口
4. 在 `context_builder` 中集成（如需要）

### 添加新 LLM 提供商

1. 在 `llm_proxy.c` 中添加新的 API 端点
2. 实现请求格式转换
3. 实现响应解析
4. 添加提供商选择逻辑
5. 更新配置常量
