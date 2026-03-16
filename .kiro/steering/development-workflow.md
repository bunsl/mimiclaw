---
inclusion: auto
---

# 开发工作流程

## 开发环境要求

- ESP-IDF v5.5+ 已安装并配置
- Python 3.10+
- CMake 3.16+
- Ninja 构建系统
- Git 2.34+
- USB 串口驱动（CP2102/CH340）

## 初始设置

```bash
# 克隆仓库
git clone https://github.com/memovai/mimiclaw.git
cd mimiclaw

# 设置目标芯片
idf.py set-target esp32s3

# 复制并编辑密钥文件
cp main/mimi_secrets.h.example main/mimi_secrets.h
# 编辑 main/mimi_secrets.h 填入你的凭证
```

## 构建流程

### 完整构建

```bash
# 清理并构建（修改 mimi_secrets.h 后必须执行）
idf.py fullclean && idf.py build
```

### 增量构建

```bash
# 仅构建修改的文件
idf.py build
```

### 清理构建

```bash
# 清理构建产物
idf.py clean

# 完全清理（包括配置）
idf.py fullclean
```

## 烧录和监控

### 查找串口

```bash
# macOS
ls /dev/cu.usb*

# Linux
ls /dev/ttyACM* /dev/ttyUSB*
```

### 烧录固件

```bash
# 烧录到设备（替换 PORT 为实际端口）
idf.py -p PORT flash

# 烧录并立即监控
idf.py -p PORT flash monitor
```

### 监控日志

```bash
# 仅监控串口输出
idf.py -p PORT monitor

# 退出监控：Ctrl+]
```

### 重要提示：USB 端口选择

ESP32-S3 开发板通常有两个 USB-C 端口：

| 端口 | 标签 | 用途 |
|------|------|------|
| USB (JTAG) | USB / JTAG | 烧录固件、JTAG 调试 |
| COM (UART) | UART / COM | 串口 CLI、REPL 交互 |

- **烧录固件**：使用 USB (JTAG) 端口
- **CLI 交互**：使用 COM (UART) 端口

## 配置修改流程

### 修改构建时配置

1. 编辑 `main/mimi_secrets.h`
2. 执行 `idf.py fullclean && idf.py build`
3. 烧录新固件

### 修改运行时配置

通过串口 CLI（无需重新编译）：

```bash
# 连接到 UART 端口
idf.py -p /dev/cu.usbserial-110 monitor

# 在 CLI 中执行命令
mimi> wifi_set MySSID MyPassword
mimi> set_api_key sk-ant-api03-xxxxx
mimi> restart
```

## 开发迭代流程

### 添加新功能

1. **规划**：确定功能属于哪个模块或需要新建模块
2. **创建文件**：在 `main/` 下创建 `.h` 和 `.c` 文件
3. **实现功能**：遵循代码风格指南
4. **更新配置**：在 `mimi_config.h` 添加必要的常量
5. **集成**：在 `mimi.c` 的 `app_main()` 中初始化
6. **测试**：构建、烧录、验证功能
7. **文档**：更新相关文档（README、ARCHITECTURE 等）

### 修复 Bug

1. **重现问题**：通过串口日志确认问题
2. **定位代码**：使用 `ESP_LOGE` 添加调试日志
3. **修复**：修改代码
4. **验证**：重新构建并测试
5. **清理**：移除调试日志或降低日志级别

### 添加新工具

1. 在 `main/tools/` 创建 `tool_<name>.c` 和 `tool_<name>.h`
2. 实现工具执行函数：
   ```c
   esp_err_t tool_<name>_execute(cJSON *input, char **output);
   ```
3. 在 `tool_registry.c` 的 `tool_registry_init()` 中注册工具
4. 定义 JSON schema（名称、描述、参数）
5. 构建并测试

## 调试技巧

### 查看内存使用

```bash
mimi> heap_info
```

### 查看 WiFi 状态

```bash
mimi> wifi_status
```

### 查看会话列表

```bash
mimi> session_list
```

### 查看长期记忆

```bash
mimi> memory_read
```

### 启用详细日志

在代码中临时添加：

```c
esp_log_level_set("module_name", ESP_LOG_DEBUG);
```

### 查看分区信息

```bash
idf.py partition-table
```

## 常见问题排查

### 构建失败

- 确保 ESP-IDF 环境已正确设置
- 检查是否执行了 `. $HOME/esp/esp-idf/export.sh`
- 尝试 `idf.py fullclean && idf.py build`

### 烧录失败

- 确认使用了正确的 USB 端口（JTAG 端口）
- 检查 USB 线缆是否支持数据传输
- 尝试按住 BOOT 按钮再烧录

### WiFi 连接失败

- 通过 CLI 检查凭证：`config_show`
- 确认 WiFi 信号强度足够
- 检查路由器是否支持 2.4GHz（ESP32-S3 不支持 5GHz）

### 内存不足

- 检查是否正确使用 PSRAM 分配大缓冲区
- 使用 `heap_info` 查看可用内存
- 减小缓冲区大小或优化内存使用

### LLM API 调用失败

- 检查 API 密钥是否正确
- 确认网络连接正常
- 如果在受限网络，配置 HTTP 代理

## 提交代码

### 分支命名

- `feat/feature-name` - 新功能
- `fix/bug-description` - Bug 修复
- `docs/update-description` - 文档更新
- `refactor/module-name` - 代码重构

### 提交信息格式

```
<type>: <简短描述>

<详细说明（可选）>

<相关 issue（可选）>
```

类型：
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档更新
- `refactor`: 代码重构
- `test`: 测试相关
- `chore`: 构建/工具相关

示例：
```
feat: 添加 GPIO 控制工具

实现了 tool_gpio 模块，支持读取和设置 GPIO 引脚状态。
包含输入/输出模式配置和上拉/下拉电阻设置。

Closes #42
```

## 测试流程

### 功能测试

1. 构建并烧录固件
2. 通过 Telegram 或 WebSocket 发送测试消息
3. 观察串口日志确认行为
4. 验证 SPIFFS 文件内容（如需要）

### 压力测试

1. 发送大量连续消息
2. 监控内存使用（`heap_info`）
3. 检查是否有内存泄漏或崩溃

### 长期运行测试

1. 让设备运行 24+ 小时
2. 定期检查日志
3. 验证 cron 和 heartbeat 功能

## 发布流程

1. 更新版本号（如有版本管理）
2. 更新 CHANGELOG（如有）
3. 完整测试所有功能
4. 创建 release tag
5. 构建并上传固件二进制文件（如需要）
