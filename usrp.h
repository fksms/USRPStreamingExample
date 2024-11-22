#ifndef __USRP_H__
#define __USRP_H__

#include <uhd.h>
#include <stdio.h>
#include <stdint.h>

// Queue size (Must be power of two)
#define RX_STREAMER_RECV_QUEUE_SIZE 64

// サンプル格納用
typedef struct _stream_buf_t
{
    // 取得したサンプル数
    size_t num_of_samples;
    // 取得したサンプル
    int16_t samples[];
} stream_buf_t;

uhd_usrp_handle usrp_setup(void);
void *usrp_stream_thread(void *arg);
void usrp_close(uhd_usrp_handle usrp);

#endif