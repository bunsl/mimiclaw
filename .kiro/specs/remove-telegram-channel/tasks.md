# 实施计划：移除 Telegram 通道

## 概述

本实施计划将从 MimiClaw ESP32-S3 AI 代理系统中完全移除 Telegram 通道支持。实施过程分为代码移除、构建配置更新、文档更新和验证测试四个阶段。所有修改都基于 C 语言和 ESP-IDF 框架。

## 任务清单

- [x] 1. 删除 Telegram 源文件
  - 删除 `main/channels/telegram/telegram_bot.c` 文件
  - 删除 `main/channels/telegram/telegram_bot.h` 文件
  - 删除 `main/channels/telegram/` 目录（如果为空）
  - _需求：1.1, 1.2, 1.3_

- [x] 2. 修改主程序文件 (main/mimi.c)
  - [x] 2.1 移除 Telegram 头文件引用
    - 在文件顶部（约第 14 行）移除 `#include "channels/telegram/telegram_bot.h"`
    - _需求：3.1_
  
  - [x] 2.2 移除 Telegram 初始化调用
    - 在 `app_main()` 函数中（约第 120 行）移除 `telegram_bot_init()` 调用
    - _需求：3.2_
  
  - [x] 2.3 移除 Telegram 启动调用
    - 在 `app_main()` 函数中（约第 180 行）移除 `telegram_bot_start()` 调用
    - _需求：3.3_
  
  - [x] 2.4 修改出站分发任务函数
    - 在 `outbound_dispatch_task()` 函数中（约第 70-100 行）移除 Telegram 通道的条件分支
    - 保留 feishu、websocket、system 分支
    - 确保 `else` 分支记录警告日志："Unknown channel: %s"
    - 确保所有分支都正确调用 `free(msg.content)`
    - _需求：4.1, 4.2, 4.3, 4.4, 4.5_

- [x] 3. 修改配置文件 (main/mimi_config.h)
  - [x] 3.1 移除 Telegram 密钥定义
    - 移除 `MIMI_SECRET_TG_TOKEN` 宏定义（约第 10-20 行）
    - _需求：5.1_
  
  - [x] 3.2 移除 Telegram 配置常量
    - 移除所有 `MIMI_TG_*` 配置宏（约第 50-60 行）
    - 包括：`MIMI_TG_POLL_TIMEOUT_S`, `MIMI_TG_MAX_MSG_LEN`, `MIMI_TG_POLL_STACK`, `MIMI_TG_POLL_PRIO`, `MIMI_TG_POLL_CORE`, `MIMI_TG_CARD_SHOW_MS`, `MIMI_TG_CARD_BODY_SCALE`
    - _需求：5.2_
  
  - [x] 3.3 移除 Telegram NVS 命名空间
    - 移除 `MIMI_NVS_TG` 宏定义（约第 150-160 行）
    - _需求：5.3_
  
  - [x] 3.4 移除 Telegram NVS 键定义
    - 移除 `MIMI_NVS_KEY_TG_TOKEN` 宏定义（约第 165-170 行）
    - _需求：5.4_

- [x] 4. 修改构建配置 (main/CMakeLists.txt)
  - 从 `idf_component_register` 的 SRCS 列表中移除 `"channels/telegram/telegram_bot.c"`
  - _需求：2.1_

- [x] 5. 修改消息总线头文件 (main/bus/message_bus.h)
  - 移除 `#define MIMI_CHAN_TELEGRAM "telegram"` 宏定义（约第 6-12 行）
  - 保留 `MIMI_CHAN_FEISHU`, `MIMI_CHAN_WEBSOCKET`, `MIMI_CHAN_CLI`, `MIMI_CHAN_SYSTEM` 定义
  - _需求：6.1, 6.2, 6.3_

- [x] 6. 检查点 - 编译验证
  - 执行 `idf.py fullclean` 清理构建缓存
  - 执行 `idf.py build` 编译项目
  - 验证编译成功，无错误和警告
  - 验证无 Telegram 相关的未定义符号错误
  - _需求：2.2, 13.1, 13.2, 13.3_

- [x] 7. 更新架构文档 (docs/ARCHITECTURE.md)
  - [ ] 7.1 更新系统架构图
    - 移除 Telegram 组件和相关连接线
    - 保留飞书和 WebSocket 通道
    - _需求：15.1_
  
  - [ ] 7.2 更新数据流说明
    - 移除 Telegram 相关的数据流描述
    - 更新消息路由说明
    - _需求：15.2_
  
  - [ ] 7.3 更新模块映射表
    - 移除 Telegram 模块条目
    - 更新通道列表
    - _需求：15.1_

- [ ] 8. 更新示例配置文件 (main/mimi_secrets.h.example)
  - 移除 `MIMI_SECRET_TG_TOKEN` 示例定义
  - 保留飞书和其他服务的配置示例
  - _需求：15.4_

- [ ] 9. 检查并清理 CLI 命令（如果存在）
  - 检查 `main/cli/serial_cli.c` 中是否有 Telegram 相关的 CLI 命令
  - 如果存在 `set_tg_token` 或类似命令，移除相关代码
  - _需求：5.1, 5.3, 5.4_

- [ ] 10. 代码搜索验证
  - 执行 `grep -r "telegram" main/ --include="*.c" --include="*.h"` 搜索残留引用
  - 执行 `grep -r "MIMI_TG" main/ --include="*.c" --include="*.h"` 搜索配置常量
  - 执行 `grep -r "MIMI_CHAN_TELEGRAM" main/ --include="*.c" --include="*.h"` 搜索通道标识符
  - 验证无匹配项（或仅在注释中）
  - _需求：1.4, 5.5_

- [ ] 11. 符号检查验证
  - 执行 `xtensa-esp32s3-elf-nm build/mimiclaw.elf | grep -i telegram` 检查二进制符号
  - 验证编译产物中无 Telegram 相关符号
  - _需求：2.3, 13.4_

- [ ] 12. 检查点 - 确保所有代码修改完成
  - 确保所有测试通过，如有疑问请询问用户

- [ ]* 13. 单元测试 - 消息路由逻辑
  - [ ]* 13.1 测试飞书消息路由
    - **属性 3: 消息路由正确性**
    - **验证：需求 4.4, 7.4**
    - 创建 channel = "feishu" 的测试消息
    - 验证 `feishu_send_message()` 被正确调用
    - 验证消息内容内存被释放
  
  - [ ]* 13.2 测试 WebSocket 消息路由
    - **属性 3: 消息路由正确性**
    - **验证：需求 4.5, 8.4**
    - 创建 channel = "websocket" 的测试消息
    - 验证 `ws_server_send()` 被正确调用
    - 验证消息内容内存被释放
  
  - [ ]* 13.3 测试未知通道处理
    - **属性 5: 未知通道处理**
    - **验证：需求 4.2, 16.1, 16.2, 16.3**
    - 创建 channel = "telegram" 的测试消息
    - 验证记录警告日志
    - 验证不调用任何发送函数
    - 验证消息内容内存被正确释放
  
  - [ ]* 13.4 测试系统消息处理
    - **属性 3: 消息路由正确性**
    - 创建 channel = "system" 的测试消息
    - 验证记录信息日志
    - 验证消息内容内存被释放

- [ ]* 14. 内存泄漏检测测试
  - **属性 4: 内存安全性**
  - **属性 10: 无内存泄漏**
  - **验证：需求 12.1, 12.2, 12.3, 12.4**
  - 启用 ESP-IDF heap tracing 工具
  - 运行系统处理多种通道的消息
  - 验证无内存泄漏
  - 验证所有消息内容内存都被正确释放

- [ ]* 15. 集成测试 - 飞书端到端消息流
  - **属性 7: 飞书消息流完整性**
  - **验证：需求 7.1, 7.2, 7.3, 7.4, 7.5**
  - 通过飞书发送测试消息
  - 验证消息到达 Inbound Queue
  - 验证 Agent Loop 处理消息
  - 验证响应通过飞书返回
  - 验证错误处理（模拟发送失败）

- [ ]* 16. 集成测试 - WebSocket 端到端消息流
  - **属性 8: WebSocket 消息流完整性**
  - **验证：需求 8.1, 8.2, 8.3, 8.4, 8.5**
  - WebSocket 客户端连接到端口 18789
  - 发送 JSON 测试消息
  - 验证响应正确返回
  - 验证错误处理（模拟发送失败）

- [ ]* 17. 集成测试 - 多通道并发测试
  - **属性 9: 错误隔离性**
  - **验证：需求 18.2**
  - 同时从飞书和 WebSocket 发送消息
  - 验证两个通道的响应都正确返回
  - 验证无消息丢失
  - 验证一个通道的错误不影响另一个通道

- [ ]* 18. 稳定性测试 - 24 小时运行测试
  - **属性 14: 系统持续运行**
  - **验证：需求 12.4, 14.3, 18.3**
  - 烧录固件到设备
  - 运行系统 24 小时
  - 验证系统稳定运行
  - 验证内存占用保持稳定
  - 验证所有核心功能正常

- [ ] 19. 运行时验证
  - 烧录固件到 ESP32-S3 设备：`idf.py flash monitor`
  - 验证启动日志中无 Telegram 相关信息
  - 验证飞书消息正常收发
  - 验证 WebSocket 消息正常收发
  - 验证内存占用减少约 80 KB
  - _需求：9.1, 9.2, 9.3, 9.4, 10.1, 10.2, 10.3, 11.1, 11.2, 11.3, 14.1, 14.2_

- [ ] 20. 功能回归测试
  - 测试飞书私聊消息收发
  - 测试飞书群聊消息收发
  - 测试 WebSocket 客户端连接
  - 测试 WebSocket 消息收发
  - 测试 Agent Loop 处理消息
  - 测试 LLM API 调用
  - 测试工具调用（web_search 等）
  - 测试会话历史保存
  - _需求：7.1, 7.2, 7.3, 7.4, 7.5, 8.1, 8.2, 8.3, 8.4, 8.5, 14.4, 18.1, 18.2, 18.3_

- [ ] 21. 最终检查点
  - 确保所有测试通过，如有疑问请询问用户

## 注意事项

- 标记为 `*` 的任务是可选的测试任务，可以跳过以加快 MVP 交付
- 每个任务都引用了具体的需求编号，便于追溯
- 检查点任务确保在关键阶段进行验证
- 属性测试验证系统的正确性属性
- 集成测试验证端到端功能
- 所有代码修改都基于 C 语言和 ESP-IDF 框架

## 资源优化预期

完成本实施计划后，系统将获得以下资源优化：

- 内存节省：约 80 KB（任务栈 12 KB + HTTP 缓冲区 8 KB + TLS 连接 60 KB）
- CPU 优化：Core 0 负载降低 10-15%
- 网络优化：减少 1 个持续 HTTPS 连接
- 代码简化：移除约 500-800 行 Telegram 相关代码

## 回滚计划

如果实施过程中出现问题，可以通过以下步骤回滚：

1. 使用 Git 恢复到移除前的提交：`git revert <commit_hash>` 或 `git reset --hard <commit_hash_before_removal>`
2. 重新编译：`idf.py fullclean && idf.py build`
3. 重新烧录：`idf.py flash`
4. 验证 Telegram 功能恢复正常
