#include "uart/bsp_uart.h"
#include "tfcard/bsp_tfcard.h"
#include "esp_log.h"

// 日志标签
static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application...");

    // 初始化 UART
    uart_init();

    // 初始化 TF 卡
    tfcard_init();

    ESP_LOGI(TAG, "Application started successfully");
}