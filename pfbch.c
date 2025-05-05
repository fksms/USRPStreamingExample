#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <complex.h>

#include <liquid/liquid.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"
#include "pfbch.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern size_t num_samps_per_once;

extern stream_data_t *stream_buffer;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

// ------------For Polyphase Channelizer-----------
extern unsigned int num_channels;
extern unsigned int delay;
extern float As;

extern float complex *channelizer_output;
extern Array_Blocking_Queue_Integer abq2;
// ------------------------------------------------

// ---------------For Burst Generator--------------
extern float complex *burst_output;
extern Array_Blocking_Queue_Integer *abq3;
// ------------------------------------------------

int channelizer_setup(firpfbch_crcf *q)
{
    // Create FIR polyphase filterbank channelizer object with prototype filter based on windowed Kaiser design
    *q = firpfbch_crcf_create_kaiser(LIQUID_ANALYZER, num_channels, delay, As);

    // Allocate a buffer for the channelizer output
    //
    // ----------------------------------------
    // チャネライザ1回あたりの確保するメモリ量
    // sizeof(float complex) * num_samps_per_once
    //
    // sizeof(float complex)：1サンプルのデータサイズ
    // num_samps_per_once   ：1回で取得するサンプル数
    // ----------------------------------------
    //
    size_t element_size = sizeof(float complex) * num_samps_per_once;
    // (FFT1回あたりの確保するメモリ量 * キューサイズ) をまとめて確保する
    // （2次元配列は取得が面倒臭いので、1次元配列を2次元配列のように利用する）
    channelizer_output = (float complex *)malloc(element_size * CHANNELIZER_OUTPUT_QUEUE_SIZE);

    return 0;
}

void *channelizer_thread(void *arg)
{
    // FIR polyphase filterbank channelizer object
    firpfbch_crcf *q = arg;

    // チャネライズ後の1チャネルあたりのサンプル数
    unsigned int num_frames = num_samps_per_once / num_channels;

    // data arrays
    float complex in[num_samps_per_once]; // time-domain input  [size: num_samps_per_once x 1]

    // Array_Blocking_Queue（abq1）の何番目に格納したかを示すインデックス
    int abq1_index = 0;

    // Array_Blocking_Queue（abq2）の何番目に格納したかを示すインデックス
    int abq2_index = 0;

    // Actual streaming
    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq1, &abq1_index))
        {
            printf("Take stream data error.\n");
            break;
        }

        // 入力データをIQから複素数形式に変換
        for (int i = 0; i < (int)num_samps_per_once; i++)
        {
            in[i] = stream_buffer[abq1_index].samples[2 * i] / 32768.f + stream_buffer[abq1_index].samples[2 * i + 1] / 32768.f * I;
        }

        // チャネライザに投入
        //
        // --------------------------------
        // チャネライザの出力は以下のようになる
        //
        // <channel>_<frame> とすると、
        // a_0, b_0, c_0, d_0, e_0, ..., a_1, b_1, c_1, d_1, e_1, ...
        // となる。
        //
        // 後段の処理で、
        // a_0, a_1, a_2, ..., a_n, b_0, b_1, b_2, ..., b_n, c_0, ...
        // となるように並べ替えを行う
        // --------------------------------
        //
        for (int i = 0; i < (int)num_frames; i++)
        {
            firpfbch_crcf_analyzer_execute(*q, &in[i * num_channels], &channelizer_output[abq2_index * num_samps_per_once + i * num_channels]);
        }

        // キューへの追加が失敗した場合は解放して終了
        if (blocking_queue_add(&abq2, abq2_index))
        {
            printf("Channelizer buffer is full.\n");
            break;
        }

// インデックスをインクリメント
#if (CHANNELIZER_OUTPUT_QUEUE_SIZE & (CHANNELIZER_OUTPUT_QUEUE_SIZE - 1)) == 0 // Queue sizeが2の冪乗の場合
        abq2_index = (abq2_index + 1) & (CHANNELIZER_OUTPUT_QUEUE_SIZE - 1);
#else
        abq2_index = (abq2_index + 1) % CHANNELIZER_OUTPUT_QUEUE_SIZE;
#endif
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int channelizer_close(firpfbch_crcf q)
{
    // メモリ解放
    free(channelizer_output);

    // destroy channelizer object
    if (firpfbch_crcf_destroy(q))
    {
        printf("Destroy channelizer object failed\n");
        return -1;
    }

    return 0;
}

int burst_generator_setup(void)
{
    // Allocate a buffer for the burst output
    //
    // ----------------------------------------
    // チャネライザ1回あたりの確保するメモリ量
    // sizeof(float complex) * num_samps_per_once
    //
    // sizeof(float complex)：1サンプルのデータサイズ
    // num_samps_per_once   ：1回で取得するサンプル数
    // ----------------------------------------
    //
    size_t element_size = sizeof(float complex) * num_samps_per_once;
    // (FFT1回あたりの確保するメモリ量 * キューサイズ) をまとめて確保する
    // （2次元配列は取得が面倒臭いので、1次元配列を2次元配列のように利用する）
    burst_output = (float complex *)malloc(element_size * CHANNELIZER_OUTPUT_QUEUE_SIZE);

    return 0;
}

void *burst_generator_thread(void *arg)
{
    // チャネライズ後の1チャネルあたりのサンプル数
    unsigned int num_frames = num_samps_per_once / num_channels;

    // Array_Blocking_Queue（abq2）の何番目に格納したかを示すインデックス
    int abq2_index = 0;

    // Array_Blocking_Queue（abq3）の何番目に格納したかを示すインデックス
    int abq3_index = 0;

    // Actual streaming
    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq2, &abq2_index))
        {
            printf("Take channelizer output error.\n");
            break;
        }

        // burstの生成
        //
        // --------------------------------
        // チャネライザの出力の並べ替えを行い、バッファに格納する
        //
        // a_0, b_0, c_0, d_0, e_0, ..., a_1, b_1, c_1, d_1, e_1, ...
        // ↓
        // a_0, a_1, a_2, ..., a_n, b_0, b_1, b_2, ..., b_n, c_0, ...
        // --------------------------------
        //
        for (int i = 0; i < (int)num_frames; i++)
        {
            for (int j = 0; j < (int)num_channels; j++)
            {
                burst_output[abq3_index * num_samps_per_once + j * num_frames + i] = channelizer_output[abq2_index * num_samps_per_once + i * num_channels + j];
            }
        }

        // キューへの追加が失敗した場合は解放して終了
        for (int i = 0; i < (int)num_channels; i++)
        {
            if (blocking_queue_add(&abq3[i], abq3_index))
            {
                printf("Burst buffer is full.\n");
                goto exit_loop;
            }
        }

// インデックスをインクリメント
#if (CHANNELIZER_OUTPUT_QUEUE_SIZE & (CHANNELIZER_OUTPUT_QUEUE_SIZE - 1)) == 0 // Queue sizeが2の冪乗の場合
        abq3_index = (abq3_index + 1) & (CHANNELIZER_OUTPUT_QUEUE_SIZE - 1);
#else
        abq3_index = (abq3_index + 1) % CHANNELIZER_OUTPUT_QUEUE_SIZE;
#endif
    }

exit_loop:

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int burst_generator_close(void)
{
    // メモリ解放
    free(burst_output);

    return 0;
}