#ifndef __LFRB_H__
#define __LFRB_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>

/* ---------------------------------------------------------------
 * IQ サンプル = int16_t × 2 (I, Q 各16bit)
 * NUM_SAMPS_PER_RECV: 1回の受信サンプル数 (2000)
 * BUF_ELEM:  バッファの総要素数 (int16_t 単位, 2のべき乗)
 * ---------------------------------------------------------------*/
#define NUM_SAMPS_PER_RECV 2000
#define ELEMS_PER_RECV (NUM_SAMPS_PER_RECV * 2) /* int16_t 単位: 4000 */
#define OUTPUT_SAMPS 16384
#define OUTPUT_ELEMS (OUTPUT_SAMPS * 2) /* int16_t 単位: 32768 */
#define BUF_ELEM 65536                  /* 2のべき乗, OUTPUT_ELEMS×2以上 */
#define BUF_MASK (BUF_ELEM - 1)

typedef int16_t iq_sample_t; /* I, Q が交互に並ぶ生配列 */

/* ---------------------------------------------------------------
 * 構造体
 * write_pos と read_pos を _Atomic にするだけで SPSC ロックフリー化できる。
 * キャッシュライン分離 (64バイト境界) でフォルス・シェアリングを防ぐ。
 * ---------------------------------------------------------------*/
typedef struct
{
    iq_sample_t buf[BUF_ELEM];
    /* 書き込みスレッドのみが更新する */
    _Alignas(64) _Atomic uint32_t write_pos;
    /* 読み込みスレッドのみが更新する */
    _Alignas(64) _Atomic uint32_t read_pos;
} LockFreeRingBuffer;

void lfrb_init(LockFreeRingBuffer *rb);
bool lfrb_write(LockFreeRingBuffer *rb, const iq_sample_t *src);
bool lfrb_read(LockFreeRingBuffer *rb, iq_sample_t *dst);

#endif