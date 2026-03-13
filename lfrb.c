#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "lfrb.h"

/* ---------------------------------------------------------------
 * 初期化
 * ---------------------------------------------------------------*/
void lfrb_init(LockFreeRingBuffer *rb)
{
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
bool lfrb_write(LockFreeRingBuffer *rb, const iq_sample_t *src)
{
    uint32_t wp = atomic_load_explicit(&rb->write_pos, memory_order_relaxed);
    uint32_t rp = atomic_load_explicit(&rb->read_pos, memory_order_acquire);

    // 書き込み位置と読み出し位置の差がバッファサイズを超える場合は溢れとみなす
    if (wp - rp + ELEMS_PER_RECV > BUF_ELEM)
        return false;

    uint32_t wi = wp & BUF_MASK;
    uint32_t tail = BUF_ELEM - wi;

    if (tail >= ELEMS_PER_RECV)
    {
        memcpy(&rb->buf[wi], src, ELEMS_PER_RECV * sizeof(iq_sample_t));
    }
    else
    {
        memcpy(&rb->buf[wi], src, tail * sizeof(iq_sample_t));
        memcpy(&rb->buf[0], src + tail, (ELEMS_PER_RECV - tail) * sizeof(iq_sample_t));
    }

    atomic_store_explicit(&rb->write_pos, wp + ELEMS_PER_RECV, memory_order_release);
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
bool lfrb_read(LockFreeRingBuffer *rb, iq_sample_t *dst)
{
    uint32_t rp = atomic_load_explicit(&rb->read_pos, memory_order_relaxed);
    uint32_t wp = atomic_load_explicit(&rb->write_pos, memory_order_acquire);

    // 書き込み位置と読み出し位置の差が必要なデータ量を満たさない場合は空とみなす
    if (wp - rp < OUTPUT_ELEMS)
        return false;

    uint32_t ri = rp & BUF_MASK;
    uint32_t tail = BUF_ELEM - ri;

    if (tail >= OUTPUT_ELEMS)
    {
        memcpy(dst, &rb->buf[ri], OUTPUT_ELEMS * sizeof(iq_sample_t));
    }
    else
    {
        memcpy(dst, &rb->buf[ri], tail * sizeof(iq_sample_t));
        memcpy(dst + tail, &rb->buf[0], (OUTPUT_ELEMS - tail) * sizeof(iq_sample_t));
    }

    atomic_store_explicit(&rb->read_pos, rp + OUTPUT_ELEMS, memory_order_release);
    return true;
}