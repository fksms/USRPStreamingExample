#include <complex.h>
#include <stdlib.h>

#include "brb.h"

// 初期化
void brb_init(BlockingRingBuffer *rb) {
    rb->write_pos = 0;
    rb->read_pos = 0;
    pthread_mutex_init(&rb->mutex, NULL);
    pthread_cond_init(&rb->not_empty, NULL);
    pthread_cond_init(&rb->not_full, NULL);
    // buf配列の初期化（ポインタ・長さ）
    for (uint32_t i = 0; i < BUF_ELEM_2; ++i) {
        rb->buf[i].ptr = NULL;
        rb->buf[i].length = 0;
    }
}

// ブロッキング書き込み
// src: mallocされた配列ポインタ, length: 配列要素数
bool brb_write(BlockingRingBuffer *rb, double complex *src, int length) {
    pthread_mutex_lock(&rb->mutex);
    // バッファが満杯なら待機
    while (rb->write_pos - rb->read_pos >= BUF_ELEM_2) {
        pthread_cond_wait(&rb->not_full, &rb->mutex);
    }
    uint32_t wi = rb->write_pos & BUF_MASK_2;
    // バッファにポインタと長さを格納
    rb->buf[wi].ptr = src;
    rb->buf[wi].length = length;
    rb->write_pos++;
    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->mutex);
    return true;
}

// ブロッキング読み出し
// dst: 配列ポインタへのポインタ, length: 配列長へのポインタ
bool brb_read(BlockingRingBuffer *rb, double complex **dst, int *length) {
    pthread_mutex_lock(&rb->mutex);
    // バッファが空なら待機
    while (rb->write_pos == rb->read_pos) {
        pthread_cond_wait(&rb->not_empty, &rb->mutex);
    }
    uint32_t ri = rb->read_pos & BUF_MASK_2;
    // ポインタと長さを取得
    *dst = rb->buf[ri].ptr;
    *length = rb->buf[ri].length;
    // バッファスロットを空に
    rb->buf[ri].ptr = NULL;
    rb->buf[ri].length = 0;
    rb->read_pos++;
    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->mutex);
    return true;
}

// リソースの解放
void brb_destroy(BlockingRingBuffer *rb) {
    pthread_mutex_destroy(&rb->mutex);
    pthread_cond_destroy(&rb->not_empty);
    pthread_cond_destroy(&rb->not_full);
    // バッファ内の未解放メモリがあればfree
    for (uint32_t i = 0; i < BUF_ELEM_2; ++i) {
        if (rb->buf[i].ptr != NULL) {
            free(rb->buf[i].ptr);
            rb->buf[i].ptr = NULL;
        }
    }
}