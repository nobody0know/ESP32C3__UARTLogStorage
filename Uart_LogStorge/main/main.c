#include "uart/bsp_uart.h"
#include "tfcard/bsp_tfcard.h"
#include "esp_log.h"
#include "ws2812/ws2812.h"


// 日志标签
static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application...");

    // 初始化 UART
    uart_init();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // 初始化 WS2812
    ws2812_init();

    // 初始化 TF 卡
    tfcard_init();

    ESP_LOGI(TAG, "Application started successfully");
}