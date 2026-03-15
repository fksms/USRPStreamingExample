#ifndef __BRB_H__
#define __BRB_H__

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "lfrb.h"

// ブロッキングリングバッファ構造体
typedef struct
{
    iq_sample_t buf[BUF_ELEM];
    uint32_t write_pos;
    uint32_t read_pos;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} BlockingRingBuffer;

void brb_init(BlockingRingBuffer *rb);
bool brb_write(BlockingRingBuffer *rb, const iq_sample_t *src);
bool brb_read(BlockingRingBuffer *rb, iq_sample_t *dst);
void brb_destroy(BlockingRingBuffer *rb);

#endif // __BRB_H__
