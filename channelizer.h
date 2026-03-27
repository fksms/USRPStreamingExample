#ifndef __CHANNELIZER_H__
#define __CHANNELIZER_H__

#include <complex.h>
#include <stdio.h>

#include <fftw3.h>

/* ---------------------------------------------------------------
 * INPUT_SAMPS: チャネライザに入力するサンプル数
 * NUM_CHANNELS: 分解するチャンネル数
 * COEF_PER_STAGE: チャンネルあたりのFIRフィルタのtap数
 * KAISER_BETA: カイザー窓のbetaパラメータ
 * ---------------------------------------------------------------*/
#define INPUT_SAMPS 10000 // 1[msec]分のサンプル数（10MHzサンプリングのため）
#define NUM_CHANNELS 50
#define COEF_PER_STAGE 16
#define KAISER_BETA 8.6

// 1チャネルあたりの時間スロット数
// （INPUT_SAMPSがNUM_CHANNELSの倍数でない場合はコンパイルエラー）
#define TIME_SLOTS (INPUT_SAMPS / NUM_CHANNELS)
#if (INPUT_SAMPS % NUM_CHANNELS) != 0
#error "INPUT_SAMPS must be a multiple of NUM_CHANNELS"
#endif

typedef struct {
    // FFTWの入出力配列とプラン
    fftw_complex in[NUM_CHANNELS];
    fftw_complex out[NUM_CHANNELS];
    fftw_plan plan;
} fftw_handle;

typedef struct {
    // 分割されたFIRフィルタ係数
    double split_filter[NUM_CHANNELS][COEF_PER_STAGE];
    // Polyphase FIRの状態
    double complex reg[NUM_CHANNELS][COEF_PER_STAGE];
    // FFTWのhandle
    fftw_handle fftw;
} channelizer_handle;

double get_channel_spacing_hz(void);
int get_valid_sorted_channel_count(void);
void get_sorted_channel_indices(int num_channels, int *sorted_idx);
int channelizer_setup(channelizer_handle *handle);
void channelizer_reset(channelizer_handle *handle);
void channelizer_process_block(int num_channels, int time_slots, int coef_per_stage,
                               double complex (*reg)[coef_per_stage], const double (*split_filter)[coef_per_stage],
                               fftw_handle *fftw, const double complex *channelizer_in, double complex *channelizer_out,
                               double *power_per_channel);
void *channelizer_thread(void *arg);
int channelizer_close(channelizer_handle *handle);

#endif // __CHANNELIZER_H__
