#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <complex.h>

#include <fftw3.h>

#include "brb.h"
#include "lfrb.h"
#include "channelizer.h"
#include "fir_kaiser.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern double rate;

extern LockFreeRingBuffer rb;
// ------------------------------------------------

int channelizer_setup(channelizer_handle *handle)
{
    // 分割されたFIRフィルタ係数へのポインタ
    double (*split_filter)[COEF_PER_STAGE] = handle->split_filter;

    // FIRフィルタのタップ数
    int order = COEF_PER_STAGE * NUM_CHANNELS;

    // FIRフィルタ係数の設計
    double filter_coef[order];
    fir_design_kaiser_lowpass(filter_coef, order, 1.0 / NUM_CHANNELS, KAISER_BETA);

    // FIRフィルタ係数をチャンネルごとに分割
    for (size_t ch = 0; ch < NUM_CHANNELS; ++ch)
    {
        for (size_t j = 0; j < COEF_PER_STAGE; ++j)
        {
            split_filter[ch][j] = filter_coef[j * NUM_CHANNELS + ch];
        }
    }

    return 0;
}

void *channelizer_thread(void *arg)
{
    unsigned int counter = 0;

    // Channelizer handle
    channelizer_handle *handle = arg;

    // `rate`と`OUTPUT_SAMPS`から待機時間を計算
    double wait_sec = (double)OUTPUT_SAMPS / rate;
    time_t sec = (time_t)wait_sec;
    long nsec = (long)((wait_sec - sec) * 1e9);
    struct timespec ts = {sec, nsec};

    // リングバッファから読み取った信号を格納するためのバッファ
    static iq_sample_t output_buf[OUTPUT_ELEMS];

    // 複素信号を格納するためのバッファ
    static double complex complex_signal[OUTPUT_SAMPS];

    // 分解した信号を格納するためのバッファ
    static double complex split_signal[NUM_CHANNELS][OUTPUT_SAMPS / NUM_CHANNELS];

    // 分割されたFIRフィルタ係数へのポインタ
    double (*split_filter)[COEF_PER_STAGE] = handle->split_filter;

    // 畳み込み用レジスタ
    static double complex reg[NUM_CHANNELS][COEF_PER_STAGE] = {0};

    // フィルタ出力用
    double complex filter_output[NUM_CHANNELS] = {0};

    // チャネライザ出力用
    //
    // 出力配列 channelizer_out の周波数配置イメージ：
    //
    // - 偶数チャネル（NUM_CHANNELS=2N）の場合
    //
    // [low freq] ↑
    //   channelizer_out[N+1]
    //   channelizer_out[N+2]
    //   ...
    //   channelizer_out[2N-1]
    //   channelizer_out[0] (Center Frequency)
    //   channelizer_out[1]
    //   ...
    //   channelizer_out[N-2]
    //   channelizer_out[N-1]
    // ↓ [high freq]
    //
    // ※channelizer_out[N] は、低い方と高い方の両端が重なっており、両方の周波数成分が半分ずつ含まれている（折り返し点）
    //
    // 例：NUM_CHANNELS=8 の場合
    //   channelizer_out[0] ...中心周波数
    //   channelizer_out[5] ～ channelizer_out[7] ...低周波側
    //   channelizer_out[1] ～ channelizer_out[3] ...高周波側
    //   channelizer_out[4] ...低・高周波の折り返し
    //
    // 周波数順に並べると以下のようなイメージ：
    //   [low freq] channelizer_out[5] [6] [7] [0] [1] [2] [3] [high freq]
    //
    //
    // - 奇数チャネル（NUM_CHANNELS=2N+1）の場合
    //
    // [low freq] ↑
    //   channelizer_out[N+1]
    //   channelizer_out[N+2]
    //   ...
    //   channelizer_out[2N]
    //   channelizer_out[0] (Center Frequency)
    //   channelizer_out[1]
    //   ...
    //   channelizer_out[N-1]
    //   channelizer_out[N]
    // ↓ [high freq]
    //
    // ※奇数の場合は折り返し点（両端が重なる点）は存在しない
    //
    // 例：NUM_CHANNELS=7 の場合
    //   channelizer_out[0] ...中心周波数
    //   channelizer_out[4] ～ channelizer_out[6] ...低周波側
    //   channelizer_out[1] ～ channelizer_out[3] ...高周波側
    //
    // 周波数順に並べると以下のようなイメージ：
    //   [low freq] channelizer_out[4] [5] [6] [0] [1] [2] [3] [high freq]
    //
    static double complex channelizer_out[NUM_CHANNELS][OUTPUT_SAMPS / NUM_CHANNELS] = {0};

    // FFTWの入出力配列とプラン
    fftw_complex in[NUM_CHANNELS];
    fftw_complex out[NUM_CHANNELS];
    fftw_plan plan = fftw_plan_dft_1d(NUM_CHANNELS, in, out, FFTW_BACKWARD, FFTW_MEASURE);

    while (atomic_load(&running))
    {
        if (!lfrb_read(&rb, output_buf))
        {
            // バッファ空の場合
            nanosleep(&ts, NULL);
            continue;
        }

        // I, Q を複素数に変換
        for (size_t j = 0; j < OUTPUT_SAMPS; ++j)
        {
            // USRPからの信号はQ0, I0, Q1, I1, ... の順で格納されているはず
            complex_signal[j] = output_buf[2 * j + 1] + output_buf[2 * j] * I;
        }

        // チャンネルごとに信号を分割
        for (size_t ch = 0; ch < NUM_CHANNELS; ++ch)
        {
            for (size_t j = 0; j < OUTPUT_SAMPS / NUM_CHANNELS; ++j)
            {
                split_signal[ch][j] = complex_signal[j * NUM_CHANNELS + ch];
            }
        }

        // レジスタを初期化
        memset(reg, 0, sizeof(reg));

        // チャネライザ出力を初期化
        memset(channelizer_out, 0, sizeof(channelizer_out));

        // チャネライザ処理
        for (size_t nn = 0; nn < OUTPUT_SAMPS / NUM_CHANNELS; ++nn)
        {
            // レジスタを右向きにシフト
            for (size_t ch = 0; ch < NUM_CHANNELS; ++ch)
            {
                for (size_t k = COEF_PER_STAGE - 1; k > 0; --k)
                {
                    reg[ch][k] = reg[ch][k - 1];
                }
                // レジスタの最左列に信号を1つずつ代入
                reg[ch][0] = split_signal[ch][nn];
            }

            // フィルタ出力を初期化
            memset(filter_output, 0, sizeof(filter_output));

            // 時間信号とフィルタを畳み込み
            for (size_t mm = 0; mm < NUM_CHANNELS; ++mm)
            {
                // reg[mm, ::-1]とsplit_filter[mm, :]の内積
                for (size_t kk = 0; kk < COEF_PER_STAGE; ++kk)
                {
                    filter_output[NUM_CHANNELS - mm - 1] += reg[mm][COEF_PER_STAGE - kk - 1] * split_filter[mm][kk];
                }
            }

            // filter_outputをinにコピー
            for (size_t i = 0; i < NUM_CHANNELS; ++i)
            {
                in[i] = filter_output[i];
            }

            // FFTWを用いてIFFTを実行
            fftw_execute(plan);

            // IFFT結果をchannelizer_outに格納
            for (size_t i = 0; i < NUM_CHANNELS; ++i)
            {
                channelizer_out[i][nn] = out[i];
            }
        }

        printf("%d\t%f\t%f\n", counter, creal(channelizer_out[0][0]), cimag(channelizer_out[0][0]));

        counter++;
    }

    // Stop
    atomic_store(&running, false);

    // FFTWプランの破棄
    fftw_destroy_plan(plan);

    return NULL;
}

int channelizer_close(void)
{
    return 0;
}