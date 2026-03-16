---
name: LED 效果库
description: RGB LED高级效果实现库，包含呼吸灯、彩虹、闪烁等多种动画效果的完整代码示例。
---

# LED 效果库

RGB LED高级效果实现指南，包含可复用的代码模块。

## 快速开始

### 基础设置

```c
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RGB_PIN 48
#define RGB_BRIGHTNESS 128  // 0-255，建议64-200

// 关闭LED
void led_off(void) {
    neopixelWrite(RGB_PIN, 0, 0, 0);
}

// 设置纯色
void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(RGB_PIN, r, g, b);
}
```

## 效果实现

### 1. 呼吸灯 (Breathing)

LED平滑地增亮和变暗，营造呼吸感。

```c
void led_breathing(uint8_t r, uint8_t g, uint8_t b, int duration_ms) {
    // duration_ms: 完整呼吸周期时间
    int steps = 50;  // 分50步完成呼吸
    int step_delay = duration_ms / (2 * steps);
    
    // 亮起
    for (int i = 0; i <= steps; i++) {
        uint8_t brightness = (i * 255) / steps;
        neopixelWrite(RGB_PIN, 
                     (r * brightness) / 255,
                     (g * brightness) / 255,
                     (b * brightness) / 255);
        vTaskDelay(step_delay / portTICK_PERIOD_MS);
    }
    
    // 暗下
    for (int i = steps; i >= 0; i--) {
        uint8_t brightness = (i * 255) / steps;
        neopixelWrite(RGB_PIN,
                     (r * brightness) / 255,
                     (g * brightness) / 255,
                     (b * brightness) / 255);
        vTaskDelay(step_delay / portTICK_PERIOD_MS);
    }
}

// 使用示例
led_breathing(255, 0, 0, 2000);  // 红色呼吸灯，2秒周期
```

### 2. 彩虹循环 (Rainbow)

LED循环显示彩虹颜色。

```c
void led_rainbow_cycle(int cycle_count, int color_delay_ms) {
    // 定义彩虹颜色
    uint8_t colors[7][3] = {
        {255, 0, 0},      // 红
        {255, 127, 0},    // 橙
        {255, 255, 0},    // 黄
        {0, 255, 0},      // 绿
        {0, 0, 255},      // 蓝
        {75, 0, 130},     // 靛
        {148, 0, 211}     // 紫
    };
    
    for (int cycle = 0; cycle < cycle_count; cycle++) {
        for (int i = 0; i < 7; i++) {
            neopixelWrite(RGB_PIN, colors[i][0], colors[i][1], colors[i][2]);
            vTaskDelay(color_delay_ms / portTICK_PERIOD_MS);
        }
    }
}

// 使用示例
led_rainbow_cycle(3, 500);  // 循环3次，每种颜色显示500ms
```

### 3. 闪烁 (Blink)

LED快速闪烁。

```c
void led_blink(uint8_t r, uint8_t g, uint8_t b, int on_ms, int off_ms, int count) {
    for (int i = 0; i < count; i++) {
        neopixelWrite(RGB_PIN, r, g, b);
        vTaskDelay(on_ms / portTICK_PERIOD_MS);
        
        neopixelWrite(RGB_PIN, 0, 0, 0);
        vTaskDelay(off_ms / portTICK_PERIOD_MS);
    }
}

// 使用示例
led_blink(0, 255, 0, 200, 200, 5);  // 绿色闪烁5次，200ms亮/灭
```

### 4. 渐变 (Fade)

LED在两种颜色之间平滑过渡。

```c
void led_fade(uint8_t r1, uint8_t g1, uint8_t b1,
              uint8_t r2, uint8_t g2, uint8_t b2,
              int duration_ms) {
    int steps = 50;
    int step_delay = duration_ms / steps;
    
    for (int i = 0; i <= steps; i++) {
        uint8_t r = r1 + (r2 - r1) * i / steps;
        uint8_t g = g1 + (g2 - g1) * i / steps;
        uint8_t b = b1 + (b2 - b1) * i / steps;
        
        neopixelWrite(RGB_PIN, r, g, b);
        vTaskDelay(step_delay / portTICK_PERIOD_MS);
    }
}

// 使用示例
led_fade(255, 0, 0, 0, 0, 255, 2000);  // 从红色渐变到蓝色，2秒
```

### 5. 脉冲 (Pulse)

LED快速脉冲，类似心跳。

```c
void led_pulse(uint8_t r, uint8_t g, uint8_t b, int pulse_count) {
    for (int i = 0; i < pulse_count; i++) {
        // 快速亮起
        for (int j = 0; j <= 255; j += 51) {
            neopixelWrite(RGB_PIN, r * j / 255, g * j / 255, b * j / 255);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        
        // 快速暗下
        for (int j = 255; j >= 0; j -= 51) {
            neopixelWrite(RGB_PIN, r * j / 255, g * j / 255, b * j / 255);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        
        // 暂停
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

// 使用示例
led_pulse(255, 165, 0, 3);  // 橙色脉冲3次
```

### 6. 状态指示灯

根据系统状态显示不同效果。

```c
typedef enum {
    LED_STATE_STARTUP,      // 启动中
    LED_STATE_CONNECTING,   // 连接中
    LED_STATE_READY,        // 就绪
    LED_STATE_BUSY,         // 忙碌
    LED_STATE_ERROR,        // 错误
    LED_STATE_OFF           // 关闭
} led_state_t;

void led_show_state(led_state_t state) {
    switch (state) {
        case LED_STATE_STARTUP:
            led_breathing(0, 0, 255, 1000);  // 蓝色呼吸
            break;
        case LED_STATE_CONNECTING:
            led_blink(255, 255, 0, 300, 300, 10);  // 黄色闪烁
            break;
        case LED_STATE_READY:
            led_set_color(0, 255, 0);  // 绿色常亮
            break;
        case LED_STATE_BUSY:
            led_pulse(0, 255, 255, 5);  // 青色脉冲
            break;
        case LED_STATE_ERROR:
            led_blink(255, 0, 0, 200, 200, 10);  // 红色闪烁
            break;
        case LED_STATE_OFF:
            led_off();
            break;
    }
}
```

## 后台任务实现

为避免阻塞主线程，在单独的任务中运行LED效果：

```c
typedef struct {
    led_state_t current_state;
    bool should_update;
} led_context_t;

static led_context_t led_ctx = {
    .current_state = LED_STATE_OFF,
    .should_update = true
};

void led_animation_task(void *pvParameters) {
    while (1) {
        if (led_ctx.should_update) {
            led_show_state(led_ctx.current_state);
            led_ctx.should_update = false;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// 初始化
void led_init(void) {
    xTaskCreate(led_animation_task, "led_task", 2048, NULL, 5, NULL);
}

// 更新状态
void led_set_state(led_state_t state) {
    led_ctx.current_state = state;
    led_ctx.should_update = true;
}
```

## 性能优化

### 内存使用
- 所有效果函数占用栈空间 < 1KB
- 无动态内存分配

### CPU使用
- 使用`vTaskDelay`避免忙轮询
- 建议LED任务优先级为5-10

### 功耗
- 低亮度(0-64): ~10mA
- 中亮度(64-128): ~20mA
- 高亮度(128-255): ~40mA

## 集成建议

1. **在mimi.c中初始化**:
   ```c
   led_init();
   led_set_state(LED_STATE_STARTUP);
   ```

2. **在WiFi连接时更新**:
   ```c
   // WiFi连接成功
   led_set_state(LED_STATE_READY);
   ```

3. **在处理请求时更新**:
   ```c
   // 开始处理
   led_set_state(LED_STATE_BUSY);
   // 处理完成
   led_set_state(LED_STATE_READY);
   ```

4. **在错误时更新**:
   ```c
   // 发生错误
   led_set_state(LED_STATE_ERROR);
   ```

## 调试技巧

- 使用`ESP_LOGI`记录LED状态变化
- 在串口监视器中观察LED行为
- 调整延迟时间以获得最佳视觉效果
- 测试不同的亮度值找到最适合的设置
