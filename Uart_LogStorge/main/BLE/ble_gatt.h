#ifndef __BLE_GATT_H__
#define __BLE_GATT_H__

#include "freertos/ringbuf.h"
#include "freertos/semphr.h"


void ble_gatt_init(void);
void ble_write_to_buffer(const char *data, size_t len);

#endif