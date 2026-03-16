# 需求文档：移除 Telegram 通道

## 引言

本需求文档定义了从 MimiClaw ESP32-S3 AI 代理系统中移除 Telegram 通道支持的功能需求、非功能需求和验收标准。移除 Telegram 通道将简化系统架构，降低资源占用，同时保持飞书和 WebSocket 通道的完整功能。

## 术语表

- **System**: MimiClaw ESP32-S3 AI 代理系统
- **Message_Bus**: 消息总线，负责在不同组件间传递消息
- **Inbound_Queue**: 入站消息队列，存储待处理的用户消息
- **Outbound_Queue**: 出站消息队列，存储待发送的响应消息
- **Channel**: 通信通道，如飞书、WebSocket、Telegram
- **Feishu_Bot**: 飞书机器人模块，通过 WebSocket 与飞书服务器通信
- **WS_Server**: WebSocket 服务器，监听端口 18789
- **Agent_Loop**: 代理循环，处理消息并生成响应
- **NVS**: 非易失性存储（Non-Volatile Storage）
- **Core_0**: ESP32-S3 的第一个 CPU 核心
- **Core_1**: ESP32-S3 的第二个 CPU 核心
- **Outbound_Dispatch**: 出站分发任务，将响应路由到对应通道

## 需求

### 需求 1：移除 Telegram 源代码

**用户故事：** 作为系统维护者，我希望从代码库中完全移除 Telegram 相关的源文件，以便简化代码结构和降低维护成本。

#### 验收标准

1. THE System SHALL 删除文件 `main/channels/telegram/telegram_bot.c`
2. THE System SHALL 删除文件 `main/channels/telegram/telegram_bot.h`
3. THE System SHALL 删除目录 `main/channels/telegram/`（如果为空）
4. WHEN 执行代码搜索时，THE System SHALL 不包含任何 Telegram 源文件引用

### 需求 2：移除 Telegram 构建配置

**用户故事：** 作为构建工程师，我希望从构建系统中移除 Telegram 相关的配置，以便确保编译过程不包含 Telegram 代码。

#### 验收标准

1. THE System SHALL 从 `main/CMakeLists.txt` 的 SRCS 列表中移除 `"channels/telegram/telegram_bot.c"`
2. WHEN 执行编译时，THE System SHALL 成功构建且不产生未定义符号错误
3. WHEN 检查编译产物时，THE System SHALL 不包含 Telegram 相关的符号

### 需求 3：移除 Telegram 初始化和启动代码

**用户故事：** 作为系统架构师，我希望从主程序中移除 Telegram 的初始化和启动逻辑，以便系统启动时不加载 Telegram 模块。

#### 验收标准

1. THE System SHALL 从 `main/mimi.c` 中移除 `#include "channels/telegram/telegram_bot.h"` 头文件引用
2. THE System SHALL 从 `app_main()` 函数中移除 `telegram_bot_init()` 调用
3. THE System SHALL 从 `app_main()` 函数中移除 `telegram_bot_start()` 调用
4. WHEN 系统启动时，THE System SHALL 不创建 Telegram 轮询任务
5. WHEN 系统启动时，THE System SHALL 不尝试连接 Telegram API

### 需求 4：移除 Telegram 消息路由逻辑

**用户故事：** 作为消息处理开发者，我希望从出站分发任务中移除 Telegram 路由分支，以便简化消息分发逻辑。

#### 验收标准

1. THE System SHALL 从 `outbound_dispatch_task()` 函数中移除 Telegram 通道的条件分支
2. WHEN 收到 channel 为 "telegram" 的消息时，THE System SHALL 记录警告日志
3. WHEN 收到 channel 为 "telegram" 的消息时，THE System SHALL 正确释放消息内容内存
4. WHEN 收到 channel 为 "feishu" 的消息时，THE System SHALL 调用 `feishu_send_message()` 函数
5. WHEN 收到 channel 为 "websocket" 的消息时，THE System SHALL 调用 `ws_server_send()` 函数

### 需求 5：移除 Telegram 配置常量

**用户故事：** 作为配置管理员，我希望从配置文件中移除所有 Telegram 相关的常量定义，以便清理配置项。

#### 验收标准

1. THE System SHALL 从 `main/mimi_config.h` 中移除 `MIMI_SECRET_TG_TOKEN` 宏定义
2. THE System SHALL 从 `main/mimi_config.h` 中移除所有 `MIMI_TG_*` 配置宏（包括 `MIMI_TG_POLL_TIMEOUT_S`、`MIMI_TG_MAX_MSG_LEN`、`MIMI_TG_POLL_STACK` 等）
3. THE System SHALL 从 `main/mimi_config.h` 中移除 `MIMI_NVS_TG` 命名空间定义
4. THE System SHALL 从 `main/mimi_config.h` 中移除 `MIMI_NVS_KEY_TG_TOKEN` 键定义
5. WHEN 编译系统时，THE System SHALL 不引用任何 Telegram 配置常量

### 需求 6：移除 Telegram 通道标识符

**用户故事：** 作为消息总线开发者，我希望从消息总线定义中移除 Telegram 通道标识符，以便统一通道管理。

#### 验收标准

1. THE System SHALL 从 `main/bus/message_bus.h` 中移除 `MIMI_CHAN_TELEGRAM` 宏定义
2. WHEN 代码中引用通道标识符时，THE System SHALL 仅支持 "feishu"、"websocket"、"cli" 和 "system"
3. THE System SHALL 保留 `MIMI_CHAN_FEISHU`、`MIMI_CHAN_WEBSOCKET`、`MIMI_CHAN_CLI` 和 `MIMI_CHAN_SYSTEM` 定义

### 需求 7：保持飞书通道功能完整

**用户故事：** 作为飞书用户，我希望在移除 Telegram 后飞书通道仍然正常工作，以便继续使用飞书与系统交互。

#### 验收标准

1. WHEN 飞书用户发送消息时，THE System SHALL 通过 WebSocket 接收消息
2. WHEN 飞书消息到达时，THE System SHALL 将消息推送到 Inbound_Queue
3. WHEN Agent_Loop 处理完消息后，THE System SHALL 将响应推送到 Outbound_Queue
4. WHEN Outbound_Dispatch 收到飞书消息时，THE System SHALL 通过 `feishu_send_message()` 发送响应
5. WHEN 飞书发送失败时，THE System SHALL 记录错误日志并继续运行

### 需求 8：保持 WebSocket 通道功能完整

**用户故事：** 作为 WebSocket 客户端用户，我希望在移除 Telegram 后 WebSocket 通道仍然正常工作，以便通过自定义客户端与系统交互。

#### 验收标准

1. WHEN WebSocket 客户端连接到端口 18789 时，THE System SHALL 接受连接
2. WHEN WebSocket 客户端发送消息时，THE System SHALL 将消息推送到 Inbound_Queue
3. WHEN Agent_Loop 处理完消息后，THE System SHALL 将响应推送到 Outbound_Queue
4. WHEN Outbound_Dispatch 收到 WebSocket 消息时，THE System SHALL 通过 `ws_server_send()` 发送响应
5. WHEN WebSocket 发送失败时，THE System SHALL 记录警告日志并继续运行

### 需求 9：内存资源优化

**用户故事：** 作为系统性能工程师，我希望通过移除 Telegram 通道来释放内存资源，以便为其他功能提供更多可用内存。

#### 验收标准

1. WHEN 系统启动后，THE System SHALL 节省至少 12 KB 的任务栈内存
2. WHEN 系统运行时，THE System SHALL 节省至少 8 KB 的 HTTP 缓冲区内存
3. WHEN 系统运行时，THE System SHALL 节省至少 60 KB 的 TLS 连接内存
4. WHEN 测量总内存占用时，THE System SHALL 相比移除前减少约 80 KB

### 需求 10：CPU 资源优化

**用户故事：** 作为系统性能工程师，我希望通过移除 Telegram 轮询任务来降低 CPU 占用，以便提高系统响应速度。

#### 验收标准

1. WHEN 系统运行时，THE System SHALL 不在 Core_0 上运行 Telegram 轮询任务
2. WHEN 测量 Core_0 负载时，THE System SHALL 相比移除前降低 10-15%
3. WHEN 系统空闲时，THE System SHALL 不执行 Telegram 长轮询 HTTP 请求

### 需求 11：网络资源优化

**用户故事：** 作为网络管理员，我希望通过移除 Telegram 连接来减少网络占用，以便降低带宽消耗。

#### 验收标准

1. WHEN 系统运行时，THE System SHALL 不维持到 Telegram API 的 HTTPS 连接
2. WHEN 测量网络连接数时，THE System SHALL 相比移除前减少 1 个持续连接
3. WHEN 系统空闲时，THE System SHALL 不产生 Telegram 相关的网络流量

### 需求 12：内存安全性

**用户故事：** 作为系统安全工程师，我希望确保移除 Telegram 后不会引入内存泄漏，以便保持系统稳定性。

#### 验收标准

1. WHEN Outbound_Dispatch 处理消息时，THE System SHALL 在处理后释放消息内容内存
2. WHEN 收到未知通道的消息时，THE System SHALL 仍然正确释放消息内容内存
3. WHEN 使用 ESP-IDF heap tracing 工具检测时，THE System SHALL 不显示内存泄漏
4. WHEN 系统运行 24 小时后，THE System SHALL 保持稳定的内存占用

### 需求 13：编译验证

**用户故事：** 作为构建工程师，我希望确保移除 Telegram 后系统能够成功编译，以便验证代码完整性。

#### 验收标准

1. WHEN 执行 `idf.py build` 命令时，THE System SHALL 成功完成编译
2. WHEN 编译完成后，THE System SHALL 不产生未定义符号错误
3. WHEN 编译完成后，THE System SHALL 不产生 Telegram 相关的警告信息
4. WHEN 检查二进制文件时，THE System SHALL 不包含 Telegram 相关的符号

### 需求 14：运行时验证

**用户故事：** 作为测试工程师，我希望验证移除 Telegram 后系统运行正常，以便确保功能完整性。

#### 验收标准

1. WHEN 系统启动时，THE System SHALL 在启动日志中不显示 Telegram 相关信息
2. WHEN 系统运行时，THE System SHALL 正常处理飞书和 WebSocket 消息
3. WHEN 系统运行 24 小时后，THE System SHALL 保持稳定运行
4. WHEN 执行功能测试时，THE System SHALL 通过所有飞书和 WebSocket 测试用例

### 需求 15：文档更新

**用户故事：** 作为文档维护者，我希望更新所有相关文档以反映 Telegram 的移除，以便用户了解系统变更。

#### 验收标准

1. THE System SHALL 更新 `docs/ARCHITECTURE.md` 中的系统架构图，移除 Telegram 组件
2. THE System SHALL 更新 `docs/ARCHITECTURE.md` 中的数据流说明，移除 Telegram 相关描述
3. THE System SHALL 更新 `README.md`，移除 Telegram 设置说明
4. THE System SHALL 更新 `main/mimi_secrets.h.example`，移除 Telegram token 示例
5. WHEN 用户查阅文档时，THE System SHALL 不包含过时的 Telegram 引用

### 需求 16：错误处理

**用户故事：** 作为系统开发者，我希望系统能够优雅地处理 Telegram 相关的遗留消息，以便避免运行时错误。

#### 验收标准

1. WHEN 收到 channel 为 "telegram" 的消息时，THE System SHALL 记录警告日志 "Unknown channel: telegram"
2. WHEN 收到 channel 为 "telegram" 的消息时，THE System SHALL 不尝试发送消息
3. WHEN 收到 channel 为 "telegram" 的消息时，THE System SHALL 正确释放消息内容内存
4. WHEN 处理未知通道消息后，THE System SHALL 继续正常处理其他消息

### 需求 17：NVS 兼容性

**用户故事：** 作为系统升级用户，我希望系统能够兼容旧版本中存储的 Telegram 配置，以便平滑升级。

#### 验收标准

1. WHEN NVS 中存在旧的 Telegram 配置时，THE System SHALL 忽略这些配置
2. WHEN 系统启动时，THE System SHALL 不尝试读取 Telegram token
3. WHEN 系统运行时，THE System SHALL 不因 NVS 中的 Telegram 数据而产生错误
4. WHEN 系统正常运行后，THE System SHALL 允许用户通过 CLI 清除 NVS 中的 Telegram 数据（可选）

### 需求 18：系统稳定性

**用户故事：** 作为系统管理员，我希望移除 Telegram 后系统保持稳定运行，以便确保服务可靠性。

#### 验收标准

1. WHEN 系统运行时，THE System SHALL 不因移除 Telegram 而崩溃
2. WHEN 一个通道发生错误时，THE System SHALL 不影响其他通道的正常运行
3. WHEN 系统运行 24 小时后，THE System SHALL 保持所有核心功能正常
4. WHEN 执行压力测试时，THE System SHALL 稳定处理并发消息

### 需求 19：回滚能力

**用户故事：** 作为系统维护者，我希望在出现问题时能够回滚到移除前的版本，以便快速恢复服务。

#### 验收标准

1. THE System SHALL 使用 Git 版本控制管理代码变更
2. WHEN 需要回滚时，THE System SHALL 能够通过 `git revert` 或 `git reset` 恢复代码
3. WHEN 回滚完成后，THE System SHALL 能够重新编译并运行
4. WHEN 回滚后运行时，THE System SHALL 恢复 Telegram 功能

### 需求 20：测试覆盖

**用户故事：** 作为质量保证工程师，我希望有完整的测试覆盖来验证移除 Telegram 的正确性，以便确保质量。

#### 验收标准

1. THE System SHALL 提供单元测试验证消息路由逻辑
2. THE System SHALL 提供集成测试验证端到端消息流
3. THE System SHALL 提供内存泄漏检测测试
4. THE System SHALL 提供 24 小时稳定性测试
5. WHEN 执行所有测试时，THE System SHALL 通过所有测试用例
