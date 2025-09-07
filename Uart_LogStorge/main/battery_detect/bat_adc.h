#include "esp_adc/adc_oneshot.h"
#define BAT_VOLTAGE_LOW 0.5f
void BAT_detect_Task(void *pvParameter);
void BAT_adc_init(void);
float BAT_adc_get_voltage(void);