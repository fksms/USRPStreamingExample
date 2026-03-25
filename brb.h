#ifndef __BRB_H__
#define __BRB_H__

#include <complex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------
 * BUF_ELEM_2: バッファのサンプル数（2の冪乗）
 * ---------------------------------------------------------------*/
#define BUF_ELEM_2 1024 // 2^10
#define BUF_MASK_2 (BUF_ELEM_2 - 1)

// BUF_ELEM_2が2の冪乗でない場合はコンパイルエラー
#if ((BUF_ELEM_2 & BUF_MASK_2) != 0)
#error "BUF_ELEM_2 must be a power of 2"
#endif

// バッファ要素：mallocした配列ポインタと長さ
typedef struct {
    double complex *ptr; // mallocされた配列のポインタ
    int len;             // mallocされた配列の要素数
} BrbElem;

// BlockingRingBuffer構造体
typedef struct {
    BrbElem buf[BUF_ELEM_2];
    int write_pos;
    int read_pos;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} BlockingRingBuffer;

bool brb_init(BlockingRingBuffer *rb);
bool brb_write(BlockingRingBuffer *rb, double complex *src, int len);
bool brb_read(BlockingRingBuffer *rb, double complex **dst, int *len);
bool brb_destroy(BlockingRingBuffer *rb);

#endif // __BRB_H__
