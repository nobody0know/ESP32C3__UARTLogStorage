/* SD card and FAT filesystem example.
   This example uses SPI peripheral to communicate with SD card.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "bsp_uart.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

// You can change the pin assignments here by changing the following 4 lines.
#define PIN_NUM_MISO GPIO_NUM_6
#define PIN_NUM_MOSI GPIO_NUM_4
#define PIN_NUM_CLK GPIO_NUM_5
#define PIN_NUM_CS GPIO_NUM_7

#define MOUNT_POINT "/sdcard"

#define MAX_CHAR_SIZE 64
// 增大缓冲区初始大小
#define BUFFER_SIZE 8192
#define WRITE_INTERVAL pdMS_TO_TICKS(500) // 1 秒写入一次
#define BUFFER_RESIZE_THRESHOLD 0.4       // 缓冲区使用达到 60% 时尝试扩容
#define BUFFER_RESIZE_STEP 2048           // 每次扩容的大小

static const char *TAG = "tfcard";
SemaphoreHandle_t tfcard_ringbuf_mutex = NULL;

void tfcard_task(void *pvParameters);

RingbufHandle_t tfcard_ringbuf = NULL;
sdmmc_card_t *card;

const char mount_point[] = MOUNT_POINT;

#ifdef CONFIG_DEBUG_PIN_CONNECTIONS
const char *names[] = {"CLK ", "MOSI", "MISO", "CS  "};
const int pins[] = {CONFIG_PIN_CLK,
                    CONFIG_PIN_MOSI,
                    CONFIG_PIN_MISO,
                    CONFIG_PIN_CS};

const int pin_count = sizeof(pins) / sizeof(pins[0]);
#if CONFIG_ENABLE_ADC_FEATURE
const int adc_channels[] = {CONFIG_ADC_PIN_CLK,
                            CONFIG_ADC_PIN_MOSI,
                            CONFIG_ADC_PIN_MISO,
                            CONFIG_ADC_PIN_CS};
#endif // CONFIG_ENABLE_ADC_FEATURE

pin_configuration_t config = {
    .names = names,
    .pins = pins,
#if CONFIG_ENABLE_ADC_FEATURE
    .adc_channels = adc_channels,
#endif
};
#endif // CONFIG_DEBUG_PIN_CONNECTIONS

#define WRITE_CHUNK_SIZE 1024 // 每次写入的数据块大小

// 修改 s_write_file 函数，在函数内部获取和释放锁
static esp_err_t s_write_file(const char *path, const char *data,size_t len)
{
    if (tfcard_ringbuf_mutex == NULL)
    {
        ESP_LOGE(TAG, "Mutex is not initialized");
        return ESP_FAIL;
    }

    // 获取互斥锁
    if (xSemaphoreTake(tfcard_ringbuf_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f;
    f = fopen(path, "ab"); // 以追加模式打开文件

    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        // 释放互斥锁
        xSemaphoreGive(tfcard_ringbuf_mutex);
        return ESP_FAIL;
    }

    const char *ptr = data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk_size = (remaining < WRITE_CHUNK_SIZE) ? remaining : WRITE_CHUNK_SIZE;
        size_t written = fwrite(ptr, 1, chunk_size, f);
        if (written != chunk_size) {
            ESP_LOGE(TAG, "Failed to write data to file");
            fclose(f);
            // 释放互斥锁
            xSemaphoreGive(tfcard_ringbuf_mutex);
            return ESP_FAIL;
        }
        ptr += written;
        remaining -= written;
    }

    // ESP_LOGI(TAG, "wirte data: %s", data);

    // fprintf(f, "%s", data);

    fclose(f);
    // ESP_LOGI(TAG, "Data written to file");

    // 释放互斥锁
    xSemaphoreGive(tfcard_ringbuf_mutex);

    return ESP_OK;
}

static esp_err_t s_read_file(const char *path)
{
    ESP_LOGI(TAG, "Reading file %s", path);
    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    char line[MAX_CHAR_SIZE];
    fgets(line, sizeof(line), f);
    fclose(f);

    // strip newline
    char *pos = strchr(line, '\n');
    if (pos)
    {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    return ESP_OK;
}

// 动态调整缓冲区大小
static bool resize_ringbuffer()
{
    static size_t buffer_size = BUFFER_SIZE;
    size_t buffer_free = xRingbufferGetCurFreeSize(tfcard_ringbuf);
    ESP_LOGI(TAG, "buffer_size: %d, buffer_free: %d", buffer_size, buffer_free);
    if ((float)buffer_free / buffer_size <= BUFFER_RESIZE_THRESHOLD)
    {
        if (xSemaphoreTake(tfcard_ringbuf_mutex, portMAX_DELAY) == pdTRUE)
        { // 获取互斥锁
            RingbufHandle_t new_ringbuf = xRingbufferCreate(buffer_size + BUFFER_RESIZE_STEP, RINGBUF_TYPE_BYTEBUF);
            if (new_ringbuf == NULL)
            {
                ESP_LOGE(TAG, "Failed to create new ring buffer for resizing");
                xSemaphoreGive(tfcard_ringbuf_mutex); // 释放互斥锁
                return false;
            }

            // 将旧缓冲区的数据转移到新缓冲区
            size_t item_size;
            void *item;
            while ((item = xRingbufferReceive(tfcard_ringbuf, &item_size, 0)) != NULL)
            {
                if (!xRingbufferSend(new_ringbuf, item, item_size, 0))
                {
                    ESP_LOGE(TAG, "Failed to transfer data to new ring buffer");
                    vRingbufferReturnItem(tfcard_ringbuf, item);
                    vRingbufferDelete(new_ringbuf);
                    xSemaphoreGive(tfcard_ringbuf_mutex); // 释放互斥锁
                    return false;
                }
                vRingbufferReturnItem(tfcard_ringbuf, item);
            }

            vRingbufferDelete(tfcard_ringbuf);
            tfcard_ringbuf = new_ringbuf;
            ESP_LOGI(TAG, "Ring buffer resized to %d bytes", buffer_size + BUFFER_RESIZE_STEP);
            buffer_size += BUFFER_RESIZE_STEP;

            xSemaphoreGive(tfcard_ringbuf_mutex); // 释放互斥锁
            return true;
        }
    }
    return false;
}

void tfcard_init(void)
{
    esp_err_t ret;

    tfcard_ringbuf_mutex = xSemaphoreCreateMutex(); // 创建互斥锁
    if (tfcard_ringbuf_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex for ring buffer");
        return;
    }

    // 创建环形缓冲区
    tfcard_ringbuf = xRingbufferCreate(BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (tfcard_ringbuf == NULL)
    {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return;
    }

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // #define CONFIG_FORMAT_SD_CARD
#ifdef CONFIG_FORMAT_SD_CARD
    ESP_LOGI(TAG, "Formatting SD card");
    ret = esp_vfs_fat_sdcard_format(mount_point, card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to format FATFS (%s)", esp_err_to_name(ret));
        return;
    }
#endif // CONFIG_FORMAT_SD_CARD

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the CONFIG_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
#ifdef CONFIG_DEBUG_PIN_CONNECTIONS
            check_sd_card_pins(&config, pin_count);
#endif
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // 创建 TF 卡任务
    xTaskCreate(tfcard_task, "tfcard_task", 4096 * 2, NULL, 5, NULL);
}

void tfcard_deinit(void)
{
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    ESP_LOGI(TAG, "Card unmounted");
}

// TF 卡任务函数
void tfcard_task(void *pvParameters)
{
    char file_path[128]; // 足够存储带序号的文件名
    int file_index = 1;
    struct stat st;

    // 注意FATFS默认是8.3文件名，长文件名需要打开FATFS_LONG_FILENAMES，才支持比较现代的文件名格式
    //  查找可用的日志文件名
    do
    {
        snprintf(file_path, sizeof(file_path), "%s/tfcard_log_data_%d.txt", MOUNT_POINT, file_index);
        file_index++;
    } while (stat(file_path, &st) == 0);

    ESP_LOGI(TAG, "Using log file: %s", file_path);

    size_t item_size;
    char *data;
    char write_buffer[1024];
    size_t buffer_index = 0;

    while (1)
    {
        // 尝试动态调整缓冲区大小
        resize_ringbuffer();

        // 从环形缓冲区读取数据
        data = (char *)xRingbufferReceive(tfcard_ringbuf, &item_size, WRITE_INTERVAL);
        if (data != NULL)
        {

            if (buffer_index + item_size < sizeof(write_buffer))
            {
                memcpy(write_buffer + buffer_index, data, item_size);
                buffer_index += item_size;
            }
            else
            {
                // 写入缓冲区已满，先写入文件
                ESP_LOGI(TAG, "Write buffer full, writing to file...");
                write_buffer[buffer_index] = '\0';
                // 调用 s_write_file 时会自动获取和释放锁
                s_write_file(file_path, write_buffer, buffer_index);
                buffer_index = 0;

                size_t remaining = item_size;
                const char *src_ptr = data;
                while (remaining > 0)
                {
                    size_t copy_size = (remaining < sizeof(write_buffer)) ? remaining : sizeof(write_buffer);
                    memcpy(write_buffer, src_ptr, copy_size);
                    buffer_index = copy_size;
                    src_ptr += copy_size;
                    remaining -= copy_size;

                    // 直接写入原始二进制数据（不需要添加终止符）
                    s_write_file(file_path, write_buffer, buffer_index); // 修改函数签名
                    buffer_index = 0;
                }
            }
            vRingbufferReturnItem(tfcard_ringbuf, (void *)data); // 归还缓冲区
        }

        // 定时写入剩余数据
        if (buffer_index > 0)
        {
            write_buffer[buffer_index] = '\0';
            // 调用 s_write_file 时会自动获取和释放锁
            s_write_file(file_path, write_buffer, buffer_index);
            buffer_index = 0;
        }

        vTaskDelay(WRITE_INTERVAL);
    }
}

// 提供一个公共函数用于向环形缓冲区写入数据，增加限流机制
void tfcard_write_to_buffer(const char *data, size_t len)
{
    if (tfcard_ringbuf != NULL)
    {
        if (xSemaphoreTake(tfcard_ringbuf_mutex, portMAX_DELAY) == pdTRUE)
        { // 获取互斥锁
            size_t free_size = xRingbufferGetCurFreeSize(tfcard_ringbuf);
            if (free_size < len)
            {
                ESP_LOGW(TAG, "Ring buffer is almost full, waiting for space...");
                while (xRingbufferGetCurFreeSize(tfcard_ringbuf) < len)
                {
                    xSemaphoreGive(tfcard_ringbuf_mutex); // 释放互斥锁
                    vTaskDelay(pdMS_TO_TICKS(100));
                    xSemaphoreTake(tfcard_ringbuf_mutex, portMAX_DELAY); // 重新获取互斥锁
                }
            }
            xRingbufferSend(tfcard_ringbuf, data, len, portMAX_DELAY);
            xSemaphoreGive(tfcard_ringbuf_mutex); // 释放互斥锁
        }
    }
}
