#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
// #include "driver/rmt.h"//新版驱动不好用，不便于检测分辨率，用回旧版
#include "esp_log.h"
#include "math.h"
#include "bsp_tfcard.h"
#include "freertos/semphr.h" 

// --- 配置 ---
#define RMT_RX_CHANNEL          RMT_CHANNEL_2 // Use a valid RX channel like 2 or 3
#define UART_PORT_FOR_DETECT    UART_NUM_1
#define UART_RX_PIN_FOR_DETECT  (GPIO_NUM_0)
#define UART_TX_PIN_FOR_DETECT  (GPIO_NUM_1)

static const char *TAG = "UART";

void uart_task(void *pvParameters);

static int autobaud_detect(uart_port_t uart_num, gpio_num_t rx_pin, gpio_num_t tx_pin)
{
    #define UART_AUTOBAUD_EN    (1 << 27)  // CONF0_REG的自动波特率使能位
    #define UART_AUTOBAUD_DIS   (0 << 27)  // CONF0_REG的自动波特率使能位

    #define UART_CONF0_REG(n)   (DR_REG_UART_BASE + (n) * 0x10000 + 0x0020)
    #define UART_LOW_PULSE_REG(n)   (DR_REG_UART_BASE + (n) * 0x10000 + 0x0028)
    #define UART_HIGH_PULSE_REG(n)   (DR_REG_UART_BASE + (n) * 0x10000 + 0x002C)

    float BAUD_RATE_TOLERANCE     = 0.05; // 15%的波特率容差

    // 标准波特率列表
    const int standard_baud_rates[] = {19200, 38400, 57600, 74880, 115200, 230400, 460800, 921600,1000000,1500000,2000000};
    const int num_standard_baud_rates = sizeof(standard_baud_rates) / sizeof(standard_baud_rates[0]);

    if(uart_num != UART_NUM_1) return -1;

    if(uart_is_driver_installed(uart_num)) {
        ESP_ERROR_CHECK(uart_driver_delete(uart_num));
    }
    // 初始化UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 1024 * 2, 0, 0, NULL, 0));
    
    
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

    // 配置自动波特率检测
    REG_SET_BIT(UART_CONF0_REG(uart_num), UART_AUTOBAUD_EN);
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 等待首帧数据
    uint8_t data[128];
    while (1)
    {
        int len = uart_read_bytes(uart_num, data, sizeof(data)-1, pdMS_TO_TICKS(200));
        if(len > 0) break;
        ESP_LOGI(TAG, "未检测到帧数据");
    }

    // 计算实际波特率
    //这两个寄存器最大值就只有0xfff（ESP32C3 TPM），导致波特率最小只能算出来19531.25，只能测19200+的波特率
    uint32_t low_pulse = REG_READ(UART_LOW_PULSE_REG(uart_num));
    uint32_t high_pulse = REG_READ(UART_HIGH_PULSE_REG(uart_num));
    uint32_t sclk_freq_hz = 0;
    uart_get_sclk_freq(UART_SCLK_APB, &sclk_freq_hz);
    float actual_baud = (float)sclk_freq_hz / ((low_pulse + high_pulse + 2) / 2.0f);
    ESP_LOGI(TAG, "实际波特率: %f", actual_baud);

    // 匹配标准波特率
    for(int j = 0; j < num_standard_baud_rates; j++) {
        if(fabs(actual_baud - standard_baud_rates[j]) < standard_baud_rates[j] * BAUD_RATE_TOLERANCE) {
            // 重新配置检测到的波特率
            uart_driver_delete(uart_num);
            uart_config.baud_rate = standard_baud_rates[j];
            ESP_LOGI(TAG, "匹配到波特率: %d", standard_baud_rates[j]);
            ESP_LOGI(TAG, "重新配置串口波特率: %d", standard_baud_rates[j]);
            ESP_ERROR_CHECK(uart_driver_install(uart_num, 1024 * 2, 0, 0, NULL, 0));
            ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
            return standard_baud_rates[j];
        }
    }
    return 0; // 未找到匹配波特率
}

// static long detect_baud_via_rmt(uart_port_t uart_num, gpio_num_t rx_pin, gpio_num_t tx_pin) {
//     uint8_t RMT_RX_CLK_DIV = 80; // RMT计数器时钟分频器 (APB CLK is 80MHz, 80/80 = 1MHz, 1 tick = 1us)
//     uint32_t RMT_TICK_PER_US = (80000000 / RMT_RX_CLK_DIV);
//     uint8_t RMT_FILTER_TICKS_THRESH = 10; // RMT接收滤波器阈值，忽略小于10 ticks (10us) 的脉冲
//     uint16_t RMT_IDLE_THRESH_US = 5000;   // 5ms空闲阈值，认为一次传输结束
//     uint8_t RMT_MEM_BLOCK_NUM = 1;
//     float BAUD_RATE_TOLERANCE = 0.15; // 15%的波特率容差
//     // 标准波特率列表
//     const int standard_baud_rates[] = {300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 74880, 115200, 230400, 460800, 921600};
//     const int num_standard_baud_rates = sizeof(standard_baud_rates) / sizeof(standard_baud_rates[0]);

//     if(uart_is_driver_installed(uart_num)) {
//         ESP_LOGI(TAG, "删除原有的UART驱动");
//         ESP_ERROR_CHECK(uart_driver_delete(uart_num));
//     }

//     // 1. 配置RMT为接收模式
//     rmt_config_t rmt_rx_config = RMT_DEFAULT_CONFIG_RX(rx_pin, RMT_RX_CHANNEL);
//     rmt_rx_config.clk_div = RMT_RX_CLK_DIV;
//     rmt_rx_config.mem_block_num = RMT_MEM_BLOCK_NUM;
//     rmt_rx_config.rx_config.filter_en = true;
//     rmt_rx_config.rx_config.filter_ticks_thresh = RMT_FILTER_TICKS_THRESH;
//     rmt_rx_config.rx_config.idle_threshold = (RMT_IDLE_THRESH_US);

//     ESP_LOGI(TAG, "安装RMT驱动...");
//     ESP_ERROR_CHECK(rmt_config(&rmt_rx_config));
//     ESP_ERROR_CHECK(rmt_driver_install(RMT_RX_CHANNEL, 1000, 0));

//     RingbufHandle_t rb = NULL;
//     ESP_ERROR_CHECK(rmt_get_ringbuf_handle(RMT_RX_CHANNEL, &rb));

//     ESP_LOGI(TAG, "启动RMT接收器，请在引脚 %d 上发送数据...", rx_pin);
//     ESP_ERROR_CHECK(rmt_rx_start(RMT_RX_CHANNEL, true));
//     size_t rx_size = 0;
//     rmt_item32_t *items = NULL;
//     long detected_baud_rate = 0;

//     while (1) {
//         items = (rmt_item32_t *)xRingbufferReceive(rb, &rx_size, pdMS_TO_TICKS(1000));
//         if (items) {
//             int start_bit_found = 0;
//             for (int i = 0; i < rx_size / sizeof(rmt_item32_t); i++) {
//                 if (items[i].level0 == 0) {
//                     uint32_t bit_duration_ticks = items[i].duration0;
//                     ESP_LOGI(TAG, "检测到起始位脉冲，宽度: %lu ticks", bit_duration_ticks);
                    
//                     double calculated_baud_rate = (double)RMT_TICK_PER_US / bit_duration_ticks;
//                     ESP_LOGI(TAG, "计算出的波特率: %.2f", calculated_baud_rate);

//                     for (int j = 0; j < num_standard_baud_rates; j++) {
//                         if (fabs(calculated_baud_rate - standard_baud_rates[j]) < standard_baud_rates[j] * BAUD_RATE_TOLERANCE) {
//                             detected_baud_rate = standard_baud_rates[j];
//                             start_bit_found = 1;
//                             break;
//                         }
//                     }
//                     if(start_bit_found) break;
//                 }
//             }
//             vRingbufferReturnItem(rb, (void *)items);
//             if(start_bit_found) break;
//         } else {
//             ESP_LOGI(TAG, "等待数据...");
//         }
//     }
//     ESP_LOGI(TAG, "停止RMT接收器...");
//     rmt_rx_stop(RMT_RX_CHANNEL);
//     rmt_driver_uninstall(RMT_RX_CHANNEL);

//     if (detected_baud_rate > 0) {
//         ESP_LOGI(TAG, "最终检测到的波特率: %ld", detected_baud_rate);
//     } else {
//         ESP_LOGE(TAG, "未能检测到有效的波特率，将使用默认值 9600");
//         detected_baud_rate = 9600;
//     }
//     return detected_baud_rate;
// }

// 初始化UART
void uart_init(void)
{
        uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_FOR_DETECT, 1024 * 10, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_FOR_DETECT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_FOR_DETECT, UART_TX_PIN_FOR_DETECT, UART_RX_PIN_FOR_DETECT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    gpio_set_pull_mode(UART_RX_PIN_FOR_DETECT, GPIO_PULLUP_ONLY); //防止设备断电后的浮动电平导致收到乱码数据

    // 创建 UART 任务
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 10, NULL);
}

// UART 任务
void uart_task(void *pvParameters)
{
    uint32_t data_len = 256;

    while (1) {
        uint8_t data[data_len];
        int len = uart_read_bytes(UART_PORT_FOR_DETECT, data, sizeof(data), pdMS_TO_TICKS(10));
        if (len) {
            // 获取系统启动以来的毫秒数
            uint32_t timestamp = esp_log_timestamp();

            // 计算小时、分钟、秒和毫秒
            uint32_t hours = timestamp / (1000 * 60 * 60);
            timestamp %= (1000 * 60 * 60);
            uint32_t minutes = timestamp / (1000 * 60);
            timestamp %= (1000 * 60);
            uint32_t seconds = timestamp / 1000;
            uint32_t milliseconds = timestamp % 1000;

            // 定义带时间戳的缓冲区
            char timestamped_data[data_len + 32]; // 为时间戳预留空间
            int timestamp_len = snprintf(timestamped_data, sizeof(timestamped_data), "[%02ld:%02ld:%02ld.%03ld] ", hours, minutes, seconds, milliseconds);
            size_t available_space = sizeof(timestamped_data) - timestamp_len - 2; // 保留换行符和终止符空间
            if (available_space > 0 && len > 0) {
                // 计算实际可拷贝长度
                size_t copy_len = (len <= available_space) ? len : available_space;
                
                memcpy(timestamped_data + timestamp_len, data, copy_len);
                timestamp_len += copy_len;
                
                // 安全添加换行符和终止符
                timestamped_data[timestamp_len] = '\n';
                timestamped_data[timestamp_len + 1] = '\0'; // 确保字符串终止
                timestamp_len++; // 包含换行符的长度
            } else {
                // 添加终止符前确保不越界
                timestamp_len = (timestamp_len < sizeof(timestamped_data)) ? timestamp_len : sizeof(timestamped_data)-1;
                timestamped_data[timestamp_len] = '\0';
                
                // 带限制的缓冲区扩容
                const size_t MAX_BUFFER_SIZE = 1024; // 1KB 最大缓冲区
                if (data_len < MAX_BUFFER_SIZE) {
                    ESP_LOGW(TAG, "数据截断，缓冲区从 %ld 扩容至 %ld", data_len, data_len * 2);
                    data_len = (data_len * 2 < MAX_BUFFER_SIZE) ? data_len * 2 : MAX_BUFFER_SIZE;
                } else {
                    ESP_LOGE(TAG, "达到最大缓冲区限制 %d，数据丢失！", MAX_BUFFER_SIZE);
                }
            }

            // ESP_LOGI(TAG, "UART接收到 %d 字节", len);
            // 将带时间戳的数据写入TF卡的环形缓冲区
            tfcard_write_to_buffer(timestamped_data, timestamp_len);
            // 回显原始数据
            uart_write_bytes(UART_PORT_FOR_DETECT, (const char *)timestamped_data, timestamp_len);
        }
        // vTaskDelay(pdMS_TO_TICKS(1));
    }
}