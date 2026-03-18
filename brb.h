#ifndef __BRB_H__
#define __BRB_H__

#include <complex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define BUF_ELEM_2 1024 // 2^10
#define BUF_MASK_2 (BUF_ELEM_2 - 1)

// BUF_ELEM_2が2の冪乗でない場合はコンパイルエラー
#if ((BUF_ELEM_2 & BUF_MASK_2) != 0)
#error "BUF_ELEM_2 must be a power of 2"
#endif

// バッファ要素：mallocした配列ポインタと長さ
typedef struct {
    double complex *ptr; // mallocされた配列のポインタ
    int length;          // 配列の要素数
} BrbElem;

// ブロッキングリングバッファ構造体
typedef struct {
    BrbElem buf[BUF_ELEM_2];
    int write_pos;
    int read_pos;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} BlockingRingBuffer;

void brb_init(BlockingRingBuffer *rb);
// 書き込み：mallocした配列ポインタと長さを格納
bool brb_write(BlockingRingBuffer *rb, double complex *src, int length);
// 読み出し：配列ポインタと長さを取得（読み出し側でfreeする）
bool brb_read(BlockingRingBuffer *rb, double complex **dst, int *length);
void brb_destroy(BlockingRingBuffer *rb);

#endif // __BRB_H__
