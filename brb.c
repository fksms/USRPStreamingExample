#include "brb.h"

// 初期化
void brb_init(BlockingRingBuffer *rb)
{
    rb->write_pos = 0;
    rb->read_pos = 0;
    pthread_mutex_init(&rb->mutex, NULL);
    pthread_cond_init(&rb->not_empty, NULL);
    pthread_cond_init(&rb->not_full, NULL);
}

// ブロッキング書き込み
bool brb_write(BlockingRingBuffer *rb, const iq_sample_t *src)
{
    pthread_mutex_lock(&rb->mutex);
    while (rb->write_pos - rb->read_pos + ELEMS_PER_RECV > BUF_ELEM)
    {
        pthread_cond_wait(&rb->not_full, &rb->mutex);
    }
    uint32_t wi = rb->write_pos & BUF_MASK;
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
    rb->write_pos += ELEMS_PER_RECV;
    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->mutex);
    return true;
}

// ブロッキング読み出し
bool brb_read(BlockingRingBuffer *rb, iq_sample_t *dst)
{
    pthread_mutex_lock(&rb->mutex);
    while (rb->write_pos - rb->read_pos < OUTPUT_ELEMS)
    {
        pthread_cond_wait(&rb->not_empty, &rb->mutex);
    }
    uint32_t ri = rb->read_pos & BUF_MASK;
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
    rb->read_pos += OUTPUT_ELEMS;
    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->mutex);
    return true;
}

// リソースの解放
void brb_destroy(BlockingRingBuffer *rb)
{
    pthread_mutex_destroy(&rb->mutex);
    pthread_cond_destroy(&rb->not_empty);
    pthread_cond_destroy(&rb->not_full);
}