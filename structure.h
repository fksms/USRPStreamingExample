#ifndef __STRUCTURE_H__
#define __STRUCTURE_H__

#include <stdio.h>
#include <stdint.h>

// Queue size (Must be power of two)
#define SAMPLES_QUEUE_SIZE 32

// サンプル格納用
typedef struct _sample_buf_t {
    // 取得したサンプル数
    size_t num_of_samples;
    // 取得したサンプル
    int16_t samples[];
} sample_buf_t;

#endif
