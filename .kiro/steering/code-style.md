---
inclusion: auto
---

# 代码风格指南

## C 代码风格

### 命名约定

- **宏定义**：全大写，使用 `MIMI_` 前缀
  ```c
  #define MIMI_MAX_BUFFER_SIZE  4096
  #define MIMI_WIFI_RETRY_MAX   10
  ```

- **函数名**：小写，使用模块前缀和下划线分隔
  ```c
  esp_err_t wifi_manager_init(void);
  esp_err_t telegram_send_message(const char *chat_id, const char *text);
  ```

- **结构体类型**：小写，使用 `_t` 后缀
  ```c
  typedef struct {
      char channel[16];
      char chat_id[32];
      char *content;
  } mimi_msg_t;
  ```

- **静态函数**：使用 `static` 关键字，不导出到头文件
  ```c
  static esp_err_t init_nvs(void)
  static void outbound_dispatch_task(void *arg)
  ```

### 代码格式

- **缩进**：4 个空格（不使用 Tab）
- **大括号**：K&R 风格（左大括号在同一行）
  ```c
  if (condition) {
      // code
  } else {
      // code
  }
  ```

- **指针声明**：星号靠近变量名
  ```c
  char *buffer;
  const char *message;
  ```

- **函数参数**：如果过长，每行一个参数
  ```c
  esp_err_t function_with_many_params(
      const char *param1,
      int param2,
      void *param3
  );
  ```

### 头文件组织

```c
#pragma once  // 使用 #pragma once 而不是 include guards

/* 系统头文件 */
#include <stdio.h>
#include <string.h>

/* FreeRTOS 头文件 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP-IDF 头文件 */
#include "esp_log.h"
#include "esp_err.h"

/* 项目头文件 */
#include "mimi_config.h"
#include "module_name.h"
```

### 错误处理

- 所有公共函数返回 `esp_err_t`
- 使用 `ESP_ERROR_CHECK` 检查关键错误
- 使用 `ESP_LOGI/W/E` 记录日志

```c
esp_err_t wifi_manager_start(void)
{
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi started successfully");
    return ESP_OK;
}
```

### 日志记录

- 每个 `.c` 文件定义静态 TAG
  ```c
  static const char *TAG = "module_name";
  ```

- 使用适当的日志级别：
  - `ESP_LOGI`: 正常操作信息
  - `ESP_LOGW`: 警告（可恢复的问题）
  - `ESP_LOGE`: 错误（严重问题）
  - `ESP_LOGD`: 调试信息（默认禁用）

### 内存管理

- **小缓冲区**（< 32 KB）：使用 `malloc` 或栈分配
  ```c
  char buffer[256];
  char *msg = malloc(1024);
  ```

- **大缓冲区**（≥ 32 KB）：使用 PSRAM
  ```c
  char *large_buf = heap_caps_calloc(1, 64 * 1024, MALLOC_CAP_SPIRAM);
  ```

- **所有权转移**：明确注释谁负责释放内存
  ```c
  // Caller must free() the returned string
  char *build_message(const char *input);
  
  // Takes ownership of content, will free() it
  esp_err_t message_bus_push(mimi_msg_t *msg);
  ```

### 注释风格

- 使用中文注释说明复杂逻辑
- 函数头部注释说明用途、参数、返回值

```c
/**
 * 初始化 WiFi 管理器
 * 
 * 配置 WiFi STA 模式并注册事件处理器
 * 
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t wifi_manager_init(void);

/* 出站分发任务：从出站队列读取并路由到通道 */
static void outbound_dispatch_task(void *arg)
{
    while (1) {
        // 从队列获取消息
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, UINT32_MAX) != ESP_OK) {
            continue;
        }
        
        // 根据通道类型分发
        if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
            telegram_send_message(msg.chat_id, msg.content);
        }
        
        free(msg.content);  // 释放消息内容
    }
}
```

## 配置管理

- 所有常量定义在 `mimi_config.h`
- 凭证和密钥定义在 `mimi_secrets.h`（gitignored）
- 使用 `#ifndef` 提供默认值

```c
#ifndef MIMI_SECRET_WIFI_SSID
#define MIMI_SECRET_WIFI_SSID  ""
#endif
```

## FreeRTOS 任务

- 任务函数签名：`void task_name(void *arg)`
- 使用 `xTaskCreatePinnedToCore` 指定 CPU 核心
- 任务栈大小使用配置常量

```c
xTaskCreatePinnedToCore(
    agent_loop_task,        // 任务函数
    "agent_loop",           // 任务名称
    MIMI_AGENT_STACK,       // 栈大小
    NULL,                   // 参数
    MIMI_AGENT_PRIO,        // 优先级
    NULL,                   // 任务句柄
    MIMI_AGENT_CORE         // CPU 核心
);
```

## 避免的做法

- ❌ 不要使用全局可变状态（除非必要且有互斥保护）
- ❌ 不要在头文件中定义函数实现
- ❌ 不要混合不同的命名风格
- ❌ 不要在 PSRAM 中分配小缓冲区（性能损失）
- ❌ 不要忘记检查 `malloc` 返回值
- ❌ 不要在中断处理程序中使用阻塞调用

## 最佳实践

- ✅ 优先使用栈分配而不是堆分配（如果大小已知且较小）
- ✅ 使用 `const` 修饰不会修改的参数
- ✅ 使用 `static` 限制函数和变量的作用域
- ✅ 在函数开始处检查参数有效性
- ✅ 保持函数简短且单一职责
- ✅ 使用有意义的变量名，避免单字母变量（除了循环计数器）
