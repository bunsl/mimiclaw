---
inclusion: auto
---

# 测试和调试指南

## 调试工具

### 串口日志

MimiClaw 的主要调试工具是 ESP-IDF 日志系统：

```c
// 日志级别（从低到高）
ESP_LOGD(TAG, "Debug message");    // 默认禁用
ESP_LOGI(TAG, "Info message");     // 正常信息
ESP_LOGW(TAG, "Warning message");  // 警告
ESP_LOGE(TAG, "Error message");    // 错误
```

**查看日志**：
```bash
idf.py -p PORT monitor
```

**动态调整日志级别**：
```c
// 在代码中临时启用详细日志
esp_log_level_set("wifi_manager", ESP_LOG_DEBUG);
esp_log_level_set("*", ESP_LOG_INFO);  // 所有模块
```

### 串口 CLI 命令

通过 UART 端口连接到设备：

```bash
# 系统信息
mimi> heap_info          # 查看内存使用
mimi> wifi_status        # WiFi 连接状态
mimi> config_show        # 显示配置（敏感信息已屏蔽）

# 会话管理
mimi> session_list       # 列出所有会话
mimi> session_clear 12345  # 清除特定会话

# 内存管理
mimi> memory_read        # 读取 MEMORY.md
mimi> memory_write "新内容"  # 写入 MEMORY.md

# 任务管理
mimi> heartbeat_trigger  # 手动触发心跳检查
mimi> cron_start         # 启动 cron 调度器

# 系统控制
mimi> restart            # 重启设备
mimi> help               # 显示所有命令
```

### 内存监控

**检查堆使用情况**：
```c
ESP_LOGI(TAG, "Internal free: %d bytes",
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
ESP_LOGI(TAG, "PSRAM free: %d bytes",
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

**检查任务栈使用**：
```c
UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Stack high water mark: %d bytes", high_water * sizeof(StackType_t));
```

**内存泄漏检测**：
```c
// 在循环前后记录内存
size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
// 执行操作...
size_t after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
ESP_LOGI(TAG, "Memory delta: %d bytes", (int)(before - after));
```

## 常见问题调试

### WiFi 连接问题

**症状**：设备无法连接到 WiFi

**调试步骤**：
1. 检查配置：
   ```bash
   mimi> config_show
   ```

2. 查看 WiFi 日志：
   ```c
   esp_log_level_set("wifi", ESP_LOG_DEBUG);
   esp_log_level_set("wifi_manager", ESP_LOG_DEBUG);
   ```

3. 扫描附近 AP：
   ```c
   wifi_manager_scan_and_print();
   ```

4. 检查常见问题：
   - SSID/密码是否正确
   - 路由器是否支持 2.4GHz（ESP32-S3 不支持 5GHz）
   - 信号强度是否足够
   - 路由器是否限制设备数量

### LLM API 调用失败

**症状**：代理无响应或返回错误

**调试步骤**：
1. 启用详细日志：
   ```c
   esp_log_level_set("llm_proxy", ESP_LOG_DEBUG);
   #define MIMI_LLM_LOG_VERBOSE_PAYLOAD 1  // 在 mimi_config.h
   ```

2. 检查 API 密钥：
   ```bash
   mimi> config_show
   ```

3. 检查网络连接：
   ```bash
   mimi> wifi_status
   ```

4. 测试 HTTPS 连接：
   - 查看日志中的 TLS 握手信息
   - 确认时间同步正确（HTTPS 需要）

5. 检查代理配置（如在受限网络）：
   ```bash
   mimi> config_show  # 查看 proxy_host 和 proxy_port
   ```

### 内存不足

**症状**：设备崩溃或重启，日志显示内存分配失败

**调试步骤**：
1. 检查当前内存使用：
   ```bash
   mimi> heap_info
   ```

2. 识别内存泄漏：
   ```c
   // 在可疑代码前后添加
   ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
   ```

3. 检查大缓冲区分配：
   - 确保 ≥32KB 的缓冲区使用 PSRAM
   - 检查是否有未释放的内存

4. 优化内存使用：
   - 减小缓冲区大小
   - 使用栈分配而不是堆分配（小缓冲区）
   - 及时释放不再使用的内存

### 任务栈溢出

**症状**：设备崩溃，日志显示 "Stack overflow" 或 "Stack canary watchpoint triggered"

**调试步骤**：
1. 识别问题任务：
   - 查看崩溃日志中的任务名称

2. 检查栈使用：
   ```c
   UBaseType_t high_water = uxTaskGetStackHighWaterMark(task_handle);
   ESP_LOGI(TAG, "Task stack remaining: %d bytes", 
            high_water * sizeof(StackType_t));
   ```

3. 增加栈大小：
   ```c
   // 在 mimi_config.h 中
   #define MIMI_TASK_STACK  (16 * 1024)  // 增加到 16 KB
   ```

4. 优化栈使用：
   - 减少局部变量大小
   - 使用堆分配大缓冲区
   - 避免深度递归

### 消息丢失

**症状**：发送的消息没有响应

**调试步骤**：
1. 检查队列状态：
   ```c
   ESP_LOGI(TAG, "Inbound queue spaces: %d", 
            uxQueueSpacesAvailable(inbound_queue));
   ```

2. 检查代理循环是否运行：
   - 查看日志中的 "Agent loop processing message"

3. 检查出站分发：
   - 查看日志中的 "Dispatching response to"

4. 检查通道发送：
   - Telegram: 查看 "Telegram send success/failed"
   - WebSocket: 查看 "WS send failed"

## 测试策略

### 单元测试（手动）

由于嵌入式环境限制，主要使用手动测试：

**测试新工具**：
1. 通过 Telegram 发送触发工具的消息
2. 观察串口日志确认工具被调用
3. 验证工具输出和 LLM 响应
4. 检查 SPIFFS 文件变化（如适用）

**测试内存管理**：
1. 在操作前后记录内存
2. 重复操作多次
3. 确认内存使用稳定

**测试错误处理**：
1. 故意触发错误条件（断开 WiFi、错误的 API 密钥等）
2. 验证错误日志和恢复行为
3. 确认设备不会崩溃

### 集成测试

**端到端测试**：
1. 发送 Telegram 消息
2. 验证代理响应
3. 检查会话历史保存
4. 验证记忆更新（如适用）

**工具链测试**：
1. 发送需要多个工具调用的复杂查询
2. 验证 ReAct 循环正确执行
3. 检查最终响应的准确性

**长期运行测试**：
1. 让设备运行 24+ 小时
2. 定期发送消息
3. 监控内存使用趋势
4. 验证 cron 和 heartbeat 功能

### 压力测试

**消息洪水测试**：
```python
# 使用 Python 脚本快速发送多条消息
import requests
import time

for i in range(100):
    send_telegram_message(f"Test message {i}")
    time.sleep(0.1)
```

**内存压力测试**：
1. 发送需要大量内存的操作
2. 监控 `heap_info`
3. 验证内存正确释放

## 调试技巧

### 使用条件编译

```c
#ifdef DEBUG_VERBOSE
    ESP_LOGI(TAG, "Detailed debug info: %s", data);
    // 打印完整的 JSON 负载
    char *json_str = cJSON_Print(json);
    ESP_LOGI(TAG, "JSON: %s", json_str);
    free(json_str);
#endif
```

### 添加断言

```c
#include <assert.h>

void process_message(const char *msg) {
    assert(msg != NULL);  // 开发时检查
    // 处理消息...
}
```

### 使用看门狗

```c
// 在长时间操作中定期喂狗
esp_task_wdt_reset();
```

### 记录关键路径

```c
ESP_LOGI(TAG, "=== Starting critical operation ===");
// 操作...
ESP_LOGI(TAG, "=== Critical operation completed ===");
```

### 使用时间戳

```c
int64_t start = esp_timer_get_time();
// 操作...
int64_t elapsed = esp_timer_get_time() - start;
ESP_LOGI(TAG, "Operation took %lld us", elapsed);
```

## 性能分析

### 测量函数执行时间

```c
#define MEASURE_TIME(name, code) do { \
    int64_t start = esp_timer_get_time(); \
    code; \
    int64_t elapsed = esp_timer_get_time() - start; \
    ESP_LOGI(TAG, "%s took %lld us", name, elapsed); \
} while(0)

// 使用
MEASURE_TIME("LLM API call", {
    llm_chat_tools(messages, tools, &response);
});
```

### 监控任务 CPU 使用

```c
// 启用 FreeRTOS 统计
#define configGENERATE_RUN_TIME_STATS 1

// 获取任务统计
char stats_buffer[1024];
vTaskGetRunTimeStats(stats_buffer);
ESP_LOGI(TAG, "Task stats:\n%s", stats_buffer);
```

## 崩溃分析

### 读取 Coredump

如果设备崩溃，coredump 会保存到 flash：

```bash
# 读取 coredump
idf.py coredump-info

# 分析 coredump
idf.py coredump-debug
```

### 解析回溯

崩溃日志中的回溯可以解析为源代码位置：

```bash
# 使用 addr2line
xtensa-esp32s3-elf-addr2line -e build/mimiclaw.elf 0x40081234
```

## 最佳实践

1. **早期添加日志**：在开发新功能时立即添加日志
2. **使用适当的日志级别**：INFO 用于正常操作，DEBUG 用于详细信息
3. **记录错误上下文**：不仅记录错误，还要记录导致错误的状态
4. **定期检查内存**：在开发过程中频繁检查内存使用
5. **测试错误路径**：不仅测试正常流程，也要测试错误处理
6. **保持日志简洁**：避免在循环中打印大量日志
7. **使用 CLI 进行现场调试**：无需重新编译即可检查状态
8. **记录性能指标**：测量关键操作的执行时间
