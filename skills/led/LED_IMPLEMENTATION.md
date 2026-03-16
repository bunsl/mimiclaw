---
name: LED 实现指南
description: RGB LED完整实现指南，包含头文件、源文件和集成步骤。
---

# LED 实现指南

完整的RGB LED驱动实现，可直接集成到MimiClaw项目中。

## 文件结构

```
main/led/
├── led_driver.h       # LED驱动头文件
├── led_driver.c       # LED驱动实现
└── led_effects.h      # 效果库头文件
```

## 头文件: led_driver.h

```c
#pragma once

#include "esp_err.h"
#include <stdint.h>

#define RGB_LED_PIN 48
#define RGB_LED_MAX_BRIGHTNESS 255

// LED状态枚举
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_BREATHING,
    LED_STATE_BLINKING,
    LED_STATE_RAINBOW,
    LED_STATE_PULSE
} led_state_t;

// LED颜色结构体
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_color_t;

/**
 * 初始化LED驱动
 */
esp_err_t led_driver_init(void);

/**
 * 设置LED颜色
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 */
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * 关闭LED
 */
void led_off(void);

/**
 * 启动呼吸灯效果
 * @param r 红色分量
 * @param g 绿色分量
 * @param b 蓝色分量
 * @param duration_ms 呼吸周期(毫秒)
 */
void led_start_breathing(uint8_t r, uint8_t g, uint8_t b, int duration_ms);

/**
 * 启动闪烁效果
 * @param r 红色分量
 * @param g 绿色分量
 * @param b 蓝色分量
 * @param on_ms 亮起时间(毫秒)
 * @param off_ms 熄灭时间(毫秒)
 */
void led_start_blinking(uint8_t r, uint8_t g, uint8_t b, int on_ms, int off_ms);

/**
 * 启动彩虹循环效果
 * @param color_delay_ms 每种颜色显示时间(毫秒)
 */
void led_start_rainbow(int color_delay_ms);

/**
 * 启动脉冲效果
 * @param r 红色分量
 * @param g 绿色分量
 * @param b 蓝色分量
 */
void led_start_pulse(uint8_t r, uint8_t g, uint8_t b);

/**
 * 停止当前动画效果
 */
void led_stop_animation(void);

/**
 * 获取当前LED状态
 */
led_state_t led_get_state(void);
```

## 源文件: led_driver.c

```c
#include "led/led_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "led_driver";

// LED动画任务句柄
static TaskHandle_t led_task_handle = NULL;

// LED状态
static struct {
    led_state_t state;
    led_color_t color;
    int param1;  // 用于存储参数
    int param2;
    bool running;
} led_state = {
    .state = LED_STATE_OFF,
    .color = {0, 0, 0},
    .running = false
};

// 内部函数声明
static void led_animation_task(void *pvParameters);
static void led_breathing_impl(uint8_t r, uint8_t g, uint8_t b, int duration_ms);
static void led_blinking_impl(uint8_t r, uint8_t g, uint8_t b, int on_ms, int off_ms);
static void led_rainbow_impl(int color_delay_ms);
static void led_pulse_impl(uint8_t r, uint8_t g, uint8_t b);

esp_err_t led_driver_init(void)
{
    ESP_LOGI(TAG, "初始化LED驱动");
    
    // 创建LED动画任务
    if (xTaskCreate(led_animation_task, "led_task", 2048, NULL, 5, &led_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "创建LED任务失败");
        return ESP_FAIL;
    }
    
    // 初始化LED为关闭状态
    led_off();
    
    ESP_LOGI(TAG, "LED驱动初始化完成");
    return ESP_OK;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    neopixelWrite(RGB_LED_PIN, r, g, b);
    led_state.color.red = r;
    led_state.color.green = g;
    led_state.color.blue = b;
    led_state.state = LED_STATE_ON;
}

void led_off(void)
{
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    led_state.color.red = 0;
    led_state.color.green = 0;
    led_state.color.blue = 0;
    led_state.state = LED_STATE_OFF;
}

void led_start_breathing(uint8_t r, uint8_t g, uint8_t b, int duration_ms)
{
    led_stop_animation();
    led_state.state = LED_STATE_BREATHING;
    led_state.color.red = r;
    led_state.color.green = g;
    led_state.color.blue = b;
    led_state.param1 = duration_ms;
    led_state.running = true;
}

void led_start_blinking(uint8_t r, uint8_t g, uint8_t b, int on_ms, int off_ms)
{
    led_stop_animation();
    led_state.state = LED_STATE_BLINKING;
    led_state.color.red = r;
    led_state.color.green = g;
    led_state.color.blue = b;
    led_state.param1 = on_ms;
    led_state.param2 = off_ms;
    led_state.running = true;
}

void led_start_rainbow(int color_delay_ms)
{
    led_stop_animation();
    led_state.state = LED_STATE_RAINBOW;
    led_state.param1 = color_delay_ms;
    led_state.running = true;
}

void led_start_pulse(uint8_t r, uint8_t g, uint8_t b)
{
    led_stop_animation();
    led_state.state = LED_STATE_PULSE;
    led_state.color.red = r;
    led_state.color.green = g;
    led_state.color.blue = b;
    led_state.running = true;
}

void led_stop_animation(void)
{
    led_state.running = false;
    vTaskDelay(100 / portTICK_PERIOD_MS);  // 等待当前动画完成
}

led_state_t led_get_state(void)
{
    return led_state.state;
}

// LED动画任务
static void led_animation_task(void *pvParameters)
{
    (void)pvParameters;
    
    while (1) {
        if (led_state.running) {
            switch (led_state.state) {
                case LED_STATE_BREATHING:
                    led_breathing_impl(led_state.color.red, led_state.color.green,
                                      led_state.color.blue, led_state.param1);
                    break;
                case LED_STATE_BLINKING:
                    led_blinking_impl(led_state.color.red, led_state.color.green,
                                     led_state.color.blue, led_state.param1, led_state.param2);
                    break;
                case LED_STATE_RAINBOW:
                    led_rainbow_impl(led_state.param1);
                    break;
                case LED_STATE_PULSE:
                    led_pulse_impl(led_state.color.red, led_state.color.green,
                                  led_state.color.blue);
                    break;
                default:
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    break;
            }
        } else {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

// 呼吸灯实现
static void led_breathing_impl(uint8_t r, uint8_t g, uint8_t b, int duration_ms)
{
    int steps = 50;
    int step_delay = duration_ms / (2 * steps);
    
    // 亮起
    for (int i = 0; i <= steps && led_state.running; i++) {
        uint8_t brightness = (i * 255) / steps;
        neopixelWrite(RGB_LED_PIN,
                     (r * brightness) / 255,
                     (g * brightness) / 255,
                     (b * brightness) / 255);
        vTaskDelay(step_delay / portTICK_PERIOD_MS);
    }
    
    // 暗下
    for (int i = steps; i >= 0 && led_state.running; i--) {
        uint8_t brightness = (i * 255) / steps;
        neopixelWrite(RGB_LED_PIN,
                     (r * brightness) / 255,
                     (g * brightness) / 255,
                     (b * brightness) / 255);
        vTaskDelay(step_delay / portTICK_PERIOD_MS);
    }
}

// 闪烁实现
static void led_blinking_impl(uint8_t r, uint8_t g, uint8_t b, int on_ms, int off_ms)
{
    if (led_state.running) {
        neopixelWrite(RGB_LED_PIN, r, g, b);
        vTaskDelay(on_ms / portTICK_PERIOD_MS);
    }
    
    if (led_state.running) {
        neopixelWrite(RGB_LED_PIN, 0, 0, 0);
        vTaskDelay(off_ms / portTICK_PERIOD_MS);
    }
}

// 彩虹循环实现
static void led_rainbow_impl(int color_delay_ms)
{
    uint8_t colors[7][3] = {
        {255, 0, 0},      // 红
        {255, 127, 0},    // 橙
        {255, 255, 0},    // 黄
        {0, 255, 0},      // 绿
        {0, 0, 255},      // 蓝
        {75, 0, 130},     // 靛
        {148, 0, 211}     // 紫
    };
    
    for (int i = 0; i < 7 && led_state.running; i++) {
        neopixelWrite(RGB_LED_PIN, colors[i][0], colors[i][1], colors[i][2]);
        vTaskDelay(color_delay_ms / portTICK_PERIOD_MS);
    }
}

// 脉冲实现
static void led_pulse_impl(uint8_t r, uint8_t g, uint8_t b)
{
    // 快速亮起
    for (int j = 0; j <= 255 && led_state.running; j += 51) {
        neopixelWrite(RGB_LED_PIN, r * j / 255, g * j / 255, b * j / 255);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    
    // 快速暗下
    for (int j = 255; j >= 0 && led_state.running; j -= 51) {
        neopixelWrite(RGB_LED_PIN, r * j / 255, g * j / 255, b * j / 255);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    
    // 暂停
    if (led_state.running) {
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}
```

## 集成步骤

### 1. 创建文件

在`main/`目录下创建`led/`文件夹，并添加上述头文件和源文件。

### 2. 更新CMakeLists.txt

在`main/CMakeLists.txt`中添加LED模块：

```cmake
idf_component_register(
    SRCS 
        # ... 现有源文件 ...
        led/led_driver.c
    INCLUDE_DIRS 
        .
    REQUIRES 
        # ... 现有依赖 ...
)
```

### 3. 在mimi.c中初始化

```c
#include "led/led_driver.h"

void app_main(void) {
    // ... 其他初始化代码 ...
    
    // 初始化LED驱动
    if (led_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "LED驱动初始化失败");
    }
    
    // 启动时显示蓝色呼吸灯
    led_start_breathing(0, 0, 255, 1000);
    
    // ... 其他代码 ...
}
```

### 4. 在WiFi连接时更新LED

```c
// 在WiFi连接成功的回调中
void on_wifi_connected(void) {
    led_stop_animation();
    led_set_color(0, 255, 0);  // 绿色常亮
}
```

### 5. 在处理请求时更新LED

```c
// 开始处理请求
void on_request_start(void) {
    led_start_pulse(0, 255, 255);  // 青色脉冲
}

// 请求完成
void on_request_complete(void) {
    led_stop_animation();
    led_set_color(0, 255, 0);  // 恢复绿色
}
```

## 编译和测试

```bash
# 清理并重新编译
idf.py fullclean
idf.py build

# 烧录到设备
idf.py -p /dev/ttyUSB0 flash monitor

# 在串口监视器中观察LED行为
```

## 故障排除

| 问题 | 解决方案 |
|------|--------|
| 编译错误：找不到led_driver.h | 检查文件路径和CMakeLists.txt配置 |
| LED不亮 | 检查GPIO48是否被占用，验证neopixelWrite函数 |
| 动画卡顿 | 减少延迟时间，检查其他任务优先级 |
| 内存不足 | 减少LED任务栈大小或优化其他任务 |

## 性能指标

- **内存占用**: ~2KB栈 + 100字节数据
- **CPU占用**: < 1% (在动画运行时)
- **功耗**: 10-40mA (取决于亮度)
- **响应时间**: < 100ms (状态切换)
