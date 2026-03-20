#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "lfrb.h"

/* ---------------------------------------------------------------
 * 初期化
 * ---------------------------------------------------------------*/
void lfrb_init(LockFreeRingBuffer *rb) {
    atomic_store_explicit(&rb->write_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->read_pos, 0, memory_order_relaxed);
}

/* ---------------------------------------------------------------
 * 書き込み — Writerスレッド専用
 *
 * [手順]
 *   1. read_pos を acquire ロードして空き容量を確認
 *   2. buf[] にデータを書き込む  ← ここはアトミック不要
 *   3. write_pos を release ストア ← buf書き込みの「完了宣言」
 *
 * release ストアにより、buf[]への書き込みが
 * write_pos の更新より先に他スレッドから見えることが保証される。
 * ---------------------------------------------------------------*/
bool lfrb_write(LockFreeRingBuffer *rb, const iq_sample_t *src, int len) {
    int wp = atomic_load_explicit(&rb->write_pos, memory_order_relaxed);
    int rp = atomic_load_explicit(&rb->read_pos, memory_order_acquire);

    // 書き込み位置と読み出し位置の差がバッファサイズを超える場合は溢れとみなす
    if (wp - rp + len > BUF_ELEM)
        return false;

    int wi = wp & BUF_MASK;
    int tail = BUF_ELEM - wi;

    if (tail >= len) {
        memcpy(&rb->buf[wi], src, len * sizeof(iq_sample_t));
    } else {
        memcpy(&rb->buf[wi], src, tail * sizeof(iq_sample_t));
        memcpy(&rb->buf[0], src + tail, (len - tail) * sizeof(iq_sample_t));
    }

    atomic_store_explicit(&rb->write_pos, wp + len, memory_order_release);
    return true;
}

/* ---------------------------------------------------------------
 * 読み出し — Readerスレッド専用
 *
 * [手順]
 *   1. write_pos を acquire ロードしてデータ量を確認
 *      → これによりWriterの buf[] 書き込みが可視になることが保証される
 *   2. buf[] からデータをコピー
 *   3. read_pos を release ストア
 * ---------------------------------------------------------------*/
bool lfrb_read(LockFreeRingBuffer *rb, iq_sample_t *dst, int len) {
    int rp = atomic_load_explicit(&rb->read_pos, memory_order_relaxed);
    int wp = atomic_load_explicit(&rb->write_pos, memory_order_acquire);

    // 書き込み位置と読み出し位置の差が必要なデータ量を満たさない場合は空とみなす
    if (wp - rp < len)
        return false;

    int ri = rp & BUF_MASK;
    int tail = BUF_ELEM - ri;

    if (tail >= len) {
        memcpy(dst, &rb->buf[ri], len * sizeof(iq_sample_t));
    } else {
        memcpy(dst, &rb->buf[ri], tail * sizeof(iq_sample_t));
        memcpy(dst + tail, &rb->buf[0], (len - tail) * sizeof(iq_sample_t));
    }

    atomic_store_explicit(&rb->read_pos, rp + len, memory_order_release);
    return true;
}