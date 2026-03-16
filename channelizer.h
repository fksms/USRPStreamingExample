#ifndef __CHANNELIZER_H__
#define __CHANNELIZER_H__

#include "lfrb.h"

/* ---------------------------------------------------------------
 * NUM_CHANNELS: 分解するチャンネル数
 * COEF_PER_STAGE: チャンネルあたりのFIRフィルタのtap数
 * KAISER_BETA: カイザー窓のbetaパラメータ
 * ---------------------------------------------------------------*/
#define NUM_CHANNELS 50
#define COEF_PER_STAGE 13
#define KAISER_BETA 8.6

#define TIME_SLOTS (OUTPUT_SAMPS / NUM_CHANNELS)

// OUTPUT_SAMPSがNUM_CHANNELSの倍数でない場合はコンパイルエラー
#if (OUTPUT_SAMPS % NUM_CHANNELS) != 0
#error "OUTPUT_SAMPS must be a multiple of NUM_CHANNELS"
#endif

typedef struct {
    // 分割されたFIRフィルタ係数
    double split_filter[NUM_CHANNELS][COEF_PER_STAGE];
} channelizer_handle;

void get_sorted_channel_indices(size_t num_channels, size_t *sorted_idx);
int channelizer_setup(channelizer_handle *handle);
void *channelizer_thread(void *arg);

#endif // __CHANNELIZER_H__