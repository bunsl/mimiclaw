---
name: LED RGB 控制
description: 控制ESP32-S3板载RGB LED，支持多种颜色和效果。使用neopixelWrite函数控制RGB通道。
---

# LED RGB 控制

ESP32-S3板载RGB LED控制指南。支持多种颜色效果和动画。

## 硬件信息

- **RGB LED引脚**: 48（GPIO48）
- **控制方式**: neopixelWrite函数
- **颜色通道**: 红(R)、绿(G)、蓝(B)，各通道0-255

## 基础颜色

### 纯色效果

使用`neopixelWrite(pin, red, green, blue)`设置RGB LED颜色：

| 颜色 | 红 | 绿 | 蓝 | 说明 |
|------|----|----|----|----|
| 白色 | 255 | 255 | 255 | 全亮 |
| 红色 | 255 | 0 | 0 | 仅红通道 |
| 绿色 | 0 | 255 | 0 | 仅绿通道 |
| 蓝色 | 0 | 0 | 255 | 仅蓝通道 |
| 黄色 | 255 | 255 | 0 | 红+绿 |
| 青色 | 0 | 255 | 255 | 绿+蓝 |
| 品红 | 255 | 0 | 255 | 红+蓝 |
| 黑色 | 0 | 0 | 0 | 关闭 |

### 亮度控制

通过调整RGB值来控制亮度。建议使用0-200范围避免过亮：

```c
// 低亮度白色
neopixelWrite(48, 64, 64, 64);

// 中亮度绿色
neopixelWrite(48, 0, 128, 0);

// 高亮度蓝色
neopixelWrite(48, 0, 0, 200);
```

## 动画效果

### 1. 呼吸灯效果

LED亮度缓慢增加和减少，营造呼吸感：

```c
// 呼吸灯 - 红色
for (int i = 0; i <= 255; i++) {
    neopixelWrite(48, i, 0, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
for (int i = 255; i >= 0; i--) {
    neopixelWrite(48, i, 0, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
```

### 2. 彩虹循环效果

LED循环显示不同颜色：

```c
// 彩虹循环
uint8_t colors[6][3] = {
    {255, 0, 0},     // 红
    {255, 255, 0},   // 黄
    {0, 255, 0},     // 绿
    {0, 255, 255},   // 青
    {0, 0, 255},     // 蓝
    {255, 0, 255}    // 品红
};

for (int i = 0; i < 6; i++) {
    neopixelWrite(48, colors[i][0], colors[i][1], colors[i][2]);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}
```

### 3. 闪烁效果

LED快速闪烁：

```c
// 闪烁 - 绿色
for (int i = 0; i < 5; i++) {
    neopixelWrite(48, 0, 255, 0);  // 亮
    vTaskDelay(200 / portTICK_PERIOD_MS);
    neopixelWrite(48, 0, 0, 0);    // 灭
    vTaskDelay(200 / portTICK_PERIOD_MS);
}
```

### 4. 渐变效果

LED在两种颜色之间平滑过渡：

```c
// 从红色渐变到蓝色
for (int i = 0; i <= 255; i++) {
    uint8_t r = 255 - i;
    uint8_t b = i;
    neopixelWrite(48, r, 0, b);
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
```

### 5. 状态指示

使用不同颜色表示不同状态：

| 状态 | 颜色 | RGB值 | 说明 |
|------|------|-------|------|
| 启动中 | 蓝色 | (0, 0, 255) | 系统初始化 |
| 连接中 | 黄色 | (255, 255, 0) | WiFi连接中 |
| 就绪 | 绿色 | (0, 255, 0) | 系统就绪 |
| 错误 | 红色 | (255, 0, 0) | 发生错误 |
| 忙碌 | 青色 | (0, 255, 255) | 处理中 |

## 实现建议

### 使用任务实现动画

为避免阻塞主线程，建议在单独的任务中运行LED动画：

```c
void led_animation_task(void *pvParameters) {
    while (1) {
        // 执行LED动画
        for (int i = 0; i <= 255; i++) {
            neopixelWrite(48, i, 0, 0);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        for (int i = 255; i >= 0; i--) {
            neopixelWrite(48, i, 0, 0);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}

// 创建任务
xTaskCreate(led_animation_task, "led_anim", 2048, NULL, 5, NULL);
```

### 性能考虑

- **延迟精度**: 使用`vTaskDelay`而不是`delay()`以避免阻塞
- **内存**: LED控制占用极少内存
- **功耗**: RGB LED功耗取决于亮度，建议使用中等亮度(64-128)

## 常见问题

**Q: LED不亮？**
- 检查GPIO48是否被其他功能占用
- 确认使用了`neopixelWrite`而不是`digitalWrite`
- 验证RGB值不为(0,0,0)

**Q: 颜色不对？**
- 检查RGB值的顺序（红、绿、蓝）
- 某些LED可能有不同的颜色映射

**Q: 动画卡顿？**
- 减少延迟时间
- 使用单独的任务运行动画
- 检查其他任务是否占用过多CPU

## 相关工具

- `gpio_write`: 控制普通GPIO引脚
- `gpio_read`: 读取GPIO引脚状态
- `gpio_read_all`: 读取所有GPIO状态
