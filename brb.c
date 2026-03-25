#include <complex.h>
#include <stdio.h>
#include <stdlib.h>

#include "brb.h"

/**
 * @brief BlockingRingBufferの初期化を行う。
 *
 * @param rb 初期化対象のBlockingRingBuffer構造体へのポインタ
 *
 * @return true 初期化に成功した場合、false 失敗した場合
 */
bool brb_init(BlockingRingBuffer *rb) {
    rb->write_pos = 0;
    rb->read_pos = 0;
    if (pthread_mutex_init(&rb->mutex, NULL) != 0) {
        fprintf(stderr, "[brb_init] pthread_mutex_init failed\n");
        return false;
    }
    if (pthread_cond_init(&rb->not_empty, NULL) != 0) {
        fprintf(stderr, "[brb_init] pthread_cond_init(not_empty) failed\n");
        return false;
    }
    if (pthread_cond_init(&rb->not_full, NULL) != 0) {
        fprintf(stderr, "[brb_init] pthread_cond_init(not_full) failed\n");
        return false;
    }
    // buf配列の初期化（ポインタ・長さ）
    for (uint32_t i = 0; i < BUF_ELEM_2; ++i) {
        rb->buf[i].ptr = NULL;
        rb->buf[i].len = 0;
    }
    return true;
}

/**
 * @brief バッファへのブロッキング書き込みを行う。
 *
 * @param rb BlockingRingBuffer構造体へのポインタ
 * @param src 書き込むmalloc済み配列のポインタ
 * @param len 書き込むmalloc済み配列の要素数
 *
 * @return true 書き込みに成功した場合、false 失敗した場合
 */
bool brb_write(BlockingRingBuffer *rb, double complex *src, int len) {
    if (pthread_mutex_lock(&rb->mutex) != 0) {
        fprintf(stderr, "[brb_write] pthread_mutex_lock failed\n");
        return false;
    }
    // バッファが満杯なら待機
    while (rb->write_pos - rb->read_pos >= BUF_ELEM_2) {
        if (pthread_cond_wait(&rb->not_full, &rb->mutex) != 0) {
            fprintf(stderr, "[brb_write] pthread_cond_wait(not_full) failed\n");
            pthread_mutex_unlock(&rb->mutex);
            return false;
        }
    }
    uint32_t wi = rb->write_pos & BUF_MASK_2;
    // バッファに配列のポインタと長さを格納
    rb->buf[wi].ptr = src;
    rb->buf[wi].len = len;
    // 書き込み位置を進める
    rb->write_pos++;
    if (pthread_cond_signal(&rb->not_empty) != 0) {
        fprintf(stderr, "[brb_write] pthread_cond_signal(not_empty) failed\n");
        pthread_mutex_unlock(&rb->mutex);
        return false;
    }
    if (pthread_mutex_unlock(&rb->mutex) != 0) {
        fprintf(stderr, "[brb_write] pthread_mutex_unlock failed\n");
        return false;
    }
    return true;
}

/**
 * @brief バッファからのブロッキング読み出しを行う。
 *
 * @param rb BlockingRingBuffer構造体へのポインタ
 * @param dst 読み出した配列ポインタを格納するポインタへのポインタ
 * @param len 読み出した配列の要素数を格納するポインタ
 *
 * @return true 読み出しに成功した場合、false 失敗した場合
 */
bool brb_read(BlockingRingBuffer *rb, double complex **dst, int *len) {
    if (pthread_mutex_lock(&rb->mutex) != 0) {
        fprintf(stderr, "[brb_read] pthread_mutex_lock failed\n");
        return false;
    }
    // バッファが空なら待機
    while (rb->write_pos == rb->read_pos) {
        if (pthread_cond_wait(&rb->not_empty, &rb->mutex) != 0) {
            fprintf(stderr, "[brb_read] pthread_cond_wait(not_empty) failed\n");
            pthread_mutex_unlock(&rb->mutex);
            return false;
        }
    }
    uint32_t ri = rb->read_pos & BUF_MASK_2;
    // ポインタと長さを取得
    *dst = rb->buf[ri].ptr;
    *len = rb->buf[ri].len;
    // バッファスロットを空に
    rb->buf[ri].ptr = NULL;
    rb->buf[ri].len = 0;
    // 読み出し位置を進める
    rb->read_pos++;
    if (pthread_cond_signal(&rb->not_full) != 0) {
        fprintf(stderr, "[brb_read] pthread_cond_signal(not_full) failed\n");
        pthread_mutex_unlock(&rb->mutex);
        return false;
    }
    if (pthread_mutex_unlock(&rb->mutex) != 0) {
        fprintf(stderr, "[brb_read] pthread_mutex_unlock failed\n");
        return false;
    }
    return true;
}

/**
 * @brief BlockingRingBufferのリソースを解放する。
 *
 * @param rb 解放対象のBlockingRingBuffer構造体へのポインタ
 *
 * @return true 解放に成功した場合、false 失敗した場合
 */
bool brb_destroy(BlockingRingBuffer *rb) {
    if (pthread_mutex_destroy(&rb->mutex) != 0) {
        fprintf(stderr, "[brb_destroy] pthread_mutex_destroy failed\n");
        return false;
    }
    if (pthread_cond_destroy(&rb->not_empty) != 0) {
        fprintf(stderr, "[brb_destroy] pthread_cond_destroy(not_empty) failed\n");
        return false;
    }
    if (pthread_cond_destroy(&rb->not_full) != 0) {
        fprintf(stderr, "[brb_destroy] pthread_cond_destroy(not_full) failed\n");
        return false;
    }
    // バッファ内の未解放メモリがあればfree
    for (uint32_t i = 0; i < BUF_ELEM_2; ++i) {
        if (rb->buf[i].ptr != NULL) {
            free(rb->buf[i].ptr);
            rb->buf[i].ptr = NULL;
        }
    }
    return true;
}
