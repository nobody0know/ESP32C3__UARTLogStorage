#ifndef __TF_CARD_H__
#define __TF_CARD_H__

#include "freertos/ringbuf.h"

#define TF_CARD_STATE_UNINIT 0
#define TF_CARD_STATE_INIT 1
#define TF_CARD_STATE_UNMOUNT 2
#define TF_CARD_STATE_MOUNT 3

extern RingbufHandle_t tfcard_ringbuf;
extern SemaphoreHandle_t tfcard_ringbuf_mutex;

void tfcard_init(void);
void tfcard_write_to_buffer(const char *data, size_t len);
uint8_t GetTfCardState(void);

#endif