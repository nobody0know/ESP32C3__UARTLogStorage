#include "uart/bsp_uart.h"
#include "tfcard/bsp_tfcard.h"
#include "esp_log.h"
#include "ws2812/ws2812.h"
#include "BLE/ble_gatt.h"
#include "battery_detect/bat_adc.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "sleep_wakeup.h"

// 日志标签
static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application...");

    //未装电池时不打开此功能
    // BAT_adc_init();

    // if( BAT_adc_get_voltage() > BAT_VOLTAGE_LOW)
    // {
    //     ok_led();
    // }
    // else
    // {
    //     low_battery();
    //     sleep_wakeup_init();
    //     while (Get_sleep_state() == SLEEP_STATE_ON)
    //     {
    //         vTaskDelay(100);
    //     }
    // }

    // 初始化 BLE
    ble_gatt_init();

    // 初始化 UART
    uart_init();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // 初始化 WS2812
    ws2812_init();

    // 初始化 TF 卡
    tfcard_init();

    ESP_LOGI(TAG, "Application started successfully");
}