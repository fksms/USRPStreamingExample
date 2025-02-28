#ifndef __USRP_H__
#define __USRP_H__

#include <uhd.h>
#include <stdio.h>
#include <stdint.h>

// Queue size (Must be power of two)
#define RX_STREAMER_RECV_QUEUE_SIZE 64

// USRP RX handle
typedef struct _uhd_usrp_rx_handle
{
    // USRP handle
    uhd_usrp_handle usrp;
    // RX streamer handle
    uhd_rx_streamer_handle rx_streamer;
    // RX metadata handle
    uhd_rx_metadata_handle rx_metadata;
} uhd_usrp_rx_handle;

// ストリーミングデータ格納用
typedef struct _stream_data_t
{
    // 取得したサンプル数
    size_t num_of_samples;
    // 取得したサンプル
    int16_t samples[];
} stream_data_t;

int usrp_setup(uhd_usrp_handle *usrp);
int usrp_rx_setup(uhd_usrp_rx_handle *usrp_rx);
void *usrp_stream_thread(void *arg);
int usrp_rx_close(uhd_usrp_rx_handle *usrp_rx);
int usrp_close(uhd_usrp_handle usrp);

#endif