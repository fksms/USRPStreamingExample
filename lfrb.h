#ifndef __LFRB_H__
#define __LFRB_H__

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------
 * BUF_ELEM: バッファのサンプル数（2の冪乗）
 * ---------------------------------------------------------------*/
#define BUF_ELEM 2097152 // 2^21
#define BUF_MASK (BUF_ELEM - 1)

// BUF_ELEMが2の冪乗でない場合はコンパイルエラー
#if ((BUF_ELEM & BUF_MASK) != 0)
#error "BUF_ELEM must be a power of 2"
#endif

typedef int16_t iq_sample_t; /* I, Q が交互に並ぶ生配列 */

/* ---------------------------------------------------------------
 * 構造体
 * write_pos と read_pos を _Atomic にするだけで SPSC ロックフリー化できる。
 * キャッシュライン分離 (64バイト境界) でフォルス・シェアリングを防ぐ。
 * ---------------------------------------------------------------*/
typedef struct {
    iq_sample_t buf[BUF_ELEM];
    /* 書き込みスレッドのみが更新する */
    _Alignas(64) _Atomic uint32_t write_pos;
    /* 読み込みスレッドのみが更新する */
    _Alignas(64) _Atomic uint32_t read_pos;
} LockFreeRingBuffer;

void lfrb_init(LockFreeRingBuffer *rb);
bool lfrb_write(LockFreeRingBuffer *rb, const iq_sample_t *src, int len);
bool lfrb_read(LockFreeRingBuffer *rb, iq_sample_t *dst, int len);

#endif // __LFRB_H__