#include "freertos/ringbuf.h"
extern RingbufHandle_t tfcard_ringbuf;
extern SemaphoreHandle_t tfcard_ringbuf_mutex;

void tfcard_init(void);
void tfcard_write_to_buffer(const char *data, size_t len);
