#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#include <fftw3.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"
#include "fft.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern double rate;
extern size_t num_samps_per_once;

extern stream_data_t *stream_buffer;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

// --------------------For FFT---------------------
extern int fft_size;

extern double *fft_data;
extern Array_Blocking_Queue_Integer abq2;
// ------------------------------------------------

int fft_setup(fftw_handle *fh)
{
    if ((size_t)fft_size != num_samps_per_once)
    {
        printf("FFT size is not equal to the number of samples per once.\n");
        return -1;
    }

    // FFTW3のハンドルを初期化
    fh->in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fft_size);
    fh->out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fft_size);
    fh->plan = fftw_plan_dft_1d(fft_size, fh->in, fh->out, FFTW_FORWARD, FFTW_ESTIMATE);

    // Allocate a buffer for the FFT data
    //
    // ----------------------------------------
    // FFT1回あたりの確保するメモリ量
    // sizeof(double) * fft_size
    //
    // sizeof(double)   ：FFT1ポイントのデータサイズ
    // fft_size         ：FFTのサイズ
    // ----------------------------------------
    //
    size_t element_size = sizeof(double) * fft_size;
    // (FFT1回あたりの確保するメモリ量 * キューサイズ) をまとめて確保する
    // （2次元配列は取得が面倒臭いので、1次元配列を2次元配列のように利用する）
    fft_data = (double *)malloc(element_size * FFT_DATA_QUEUE_SIZE);

    return 0;
}

void *fft_thread(void *arg)
{
    fftw_handle *fh = arg;

    // Array_Blocking_Queue（abq1）の何番目に格納したかを示すインデックス
    int abq1_index = 0;

    // Array_Blocking_Queue（abq2）の何番目に格納したかを示すインデックス
    int abq2_index = 0;

    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq1, &abq1_index))
        {
            printf("Take stream data error.\n");
            break;
        }

        // 入力データをIQから複素数形式に変換
        for (int i = 0; i < fft_size; i++)
        {
            fh->in[i][0] = (double)stream_buffer[abq1_index].samples[2 * i + 1]; // I成分
            fh->in[i][1] = (double)stream_buffer[abq1_index].samples[2 * i];     // Q成分
        }

        // FFTの実行
        fftw_execute(fh->plan);

        // パワースペクトル密度（PSD: Power Spectrum Density）（dB表記）を算出
        for (int i = 0; i < fft_size; i++)
        {
            fft_data[abq2_index * fft_size + i] = 10 * log10((fh->out[i][0] * fh->out[i][0] + fh->out[i][1] * fh->out[i][1]) / (rate / fft_size));
        }

        // キューへの追加が失敗した場合は解放して終了
        if (blocking_queue_add(&abq2, abq2_index))
        {
            printf("FFT buffer is full.\n");
            break;
        }

        // インデックスをインクリメント
#if (FFT_DATA_QUEUE_SIZE & (FFT_DATA_QUEUE_SIZE - 1)) == 0 // Queue sizeが2の冪乗の場合
        abq2_index = (abq2_index + 1) & (FFT_DATA_QUEUE_SIZE - 1);
#else
        abq2_index = (abq2_index + 1) % FFT_DATA_QUEUE_SIZE;
#endif
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int fft_close(fftw_handle fh)
{
    // メモリ解放
    free(fft_data);

    // メモリ解放
    fftw_destroy_plan(fh.plan);
    fftw_free(fh.in);
    fftw_free(fh.out);

    return 0;
}