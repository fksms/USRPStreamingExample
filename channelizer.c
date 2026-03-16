#include <complex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <fftw3.h>

#include "cfar.h"
#include "channelizer.h"
#include "fir_kaiser.h"
#include "lfrb.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern double rate;

extern LockFreeRingBuffer rb;
// ------------------------------------------------

int channelizer_setup(channelizer_handle *handle) {
    // 分割されたFIRフィルタ係数へのポインタ
    double(*split_filter)[COEF_PER_STAGE] = handle->split_filter;

    // FIRフィルタのタップ数
    int order = COEF_PER_STAGE * NUM_CHANNELS;

    // FIRフィルタ係数の設計
    double filter_coef[order];
    fir_design_kaiser_lowpass(filter_coef, order, 1.0 / NUM_CHANNELS, KAISER_BETA);

    // FIRフィルタ係数をチャンネルごとに分割
    for (size_t ch = 0; ch < NUM_CHANNELS; ++ch) {
        for (size_t j = 0; j < COEF_PER_STAGE; ++j) {
            split_filter[ch][j] = filter_coef[j * NUM_CHANNELS + ch];
        }
    }

    return 0;
}

void *channelizer_thread(void *arg) {
    // unsigned int counter = 0;

    // Channelizer handle
    channelizer_handle *handle = arg;

    // `rate`と`OUTPUT_SAMPS`から待機時間を計算
    double wait_sec = (double)OUTPUT_SAMPS / rate;
    time_t sec = (time_t)wait_sec;
    long nsec = (long)((wait_sec - sec) * 1e9);
    struct timespec ts = {sec, nsec};

    // リングバッファから読み取った信号を格納するためのバッファ
    static iq_sample_t output_buf[OUTPUT_ELEMS];

    /* ---------------------- ここからチャネライザ用変数 ---------------------- */

    // 複素信号を格納するためのバッファ
    static double complex complex_signal[OUTPUT_SAMPS];

    // 分解した信号を格納するためのバッファ
    static double complex split_signal[NUM_CHANNELS][TIME_SLOTS];

    // 分割されたFIRフィルタ係数へのポインタ
    double(*split_filter)[COEF_PER_STAGE] = handle->split_filter;

    // 畳み込み用レジスタ
    static double complex reg[NUM_CHANNELS][COEF_PER_STAGE] = {0};

    // フィルタ出力用バッファ
    double complex filter_output[NUM_CHANNELS] = {0};

    // FFTWの入出力配列とプラン
    fftw_complex in[NUM_CHANNELS];
    fftw_complex out[NUM_CHANNELS];
    fftw_plan plan = fftw_plan_dft_1d(NUM_CHANNELS, in, out, FFTW_BACKWARD, FFTW_MEASURE);

    // チャネライザ出力用バッファ
    static double complex channelizer_out[NUM_CHANNELS][TIME_SLOTS] = {0};

    /* ---------------------- ここまでチャネライザ用変数 ---------------------- */

    /* ---------------------- ここからCFAR用変数 ---------------------- */

    // 各チャンネルの信号の電力を格納するバッファ
    double power[NUM_CHANNELS] = {0};

    // チャネル順並び替え用配列
    // （channelizer_out の周波数配置は以下のコメントのようになっているので、周波数順に並び替えるためのインデックス配列を定義）
    size_t sorted_idx[NUM_CHANNELS];
    get_sorted_channel_indices(NUM_CHANNELS, sorted_idx);

    // CFARを実施するチャネル数
    // （偶数チャネルの場合は折り返し点を除外する）
    size_t sorted_len = NUM_CHANNELS;
    if (NUM_CHANNELS % 2 == 0)
        sorted_len--; // 偶数時は折り返し点除外

    // CFAR判定結果格納用
    bool cfar_result[NUM_CHANNELS] = {0};

    /* ---------------------- ここまでCFAR用変数 ---------------------- */

    while (atomic_load(&running)) {
        if (!lfrb_read(&rb, output_buf)) {
            // バッファ空の場合
            nanosleep(&ts, NULL);
            continue;
        }

        // I, Q を複素数に変換
        for (size_t j = 0; j < OUTPUT_SAMPS; ++j) {
            // USRPからの信号はQ0, I0, Q1, I1, ... の順で格納されているはず
            complex_signal[j] = output_buf[2 * j + 1] + output_buf[2 * j] * I;
        }

        // チャンネルごとに信号を分割
        for (size_t ch = 0; ch < NUM_CHANNELS; ++ch) {
            for (size_t j = 0; j < TIME_SLOTS; ++j) {
                split_signal[ch][j] = complex_signal[j * NUM_CHANNELS + ch];
            }
        }

        // レジスタを初期化（初期化しなくても良いっぽいので、一旦コメントアウト）
        // memset(reg, 0, sizeof(reg));

        // チャネライザ出力を初期化（そもそも初期化する必要がないためコメントアウト）
        // memset(channelizer_out, 0, sizeof(channelizer_out));

        // 信号電力を初期化
        memset(power, 0, sizeof(power));

        // ---------------------- チャネライザ処理 ----------------------
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
        // -----------------------------------------------------------
        for (size_t nn = 0; nn < TIME_SLOTS; ++nn) {
            // レジスタを右向きにシフト
            for (size_t ch = 0; ch < NUM_CHANNELS; ++ch) {
                for (size_t k = COEF_PER_STAGE - 1; k > 0; --k) {
                    reg[ch][k] = reg[ch][k - 1];
                }
                // レジスタの最左列に信号を1つずつ代入
                reg[ch][0] = split_signal[ch][nn];
            }

            // フィルタ出力を初期化
            memset(filter_output, 0, sizeof(filter_output));

            // 時間信号とフィルタを畳み込み
            for (size_t mm = 0; mm < NUM_CHANNELS; ++mm) {
                // reg[mm, ::-1]とsplit_filter[mm, :]の内積
                for (size_t kk = 0; kk < COEF_PER_STAGE; ++kk) {
                    filter_output[NUM_CHANNELS - mm - 1] += reg[mm][COEF_PER_STAGE - kk - 1] * split_filter[mm][kk];
                }
            }

            // filter_outputをinにコピー
            for (size_t i = 0; i < NUM_CHANNELS; ++i) {
                in[i] = filter_output[i];
            }

            // FFTWを用いてIFFTを実行
            fftw_execute(plan);

            // IFFT結果をchannelizer_outに格納
            // （信号の電力も同時に計算する）
            for (size_t i = 0; i < NUM_CHANNELS; ++i) {
                channelizer_out[i][nn] = out[i];
                double mag = cabs(out[i]);
                power[i] += mag * mag;
            }
        }

        // ---------------------- CFAR処理 ----------------------
        // 概要：
        // 各チャネルの信号電力（power）に対して、周波数順に並び替えた配列(sorted_idx)を用いてCFAR判定を行う。
        // CFARは、CUT（判定対象チャネル）の両側にGUARD領域を設け、その外側のTRAIN領域の平均電力を基準とし、
        // CUTの電力がα倍を超えていれば検出とする。
        //
        // 両側CFAR条件（NUM_CHANNELS=20, GUARD=1, TRAIN=2）：
        //   ...
        //   ch[7]   [TRAIN]
        //   ch[8]   [TRAIN]
        //   ch[9]   [GUARD]
        //   ch[10]  [CUT]
        //   ch[11]  [GUARD]
        //   ch[12]  [TRAIN]
        //   ch[13]  [TRAIN]
        //   ...
        //
        // 片側CFAR条件（NUM_CHANNELS=20, GUARD=1, TRAIN=2）：
        //   ch[0]   [GUARD]
        //   ch[1]   [CUT]
        //   ch[2]   [GUARD]
        //   ch[3]   [TRAIN]
        //   ch[4]   [TRAIN]
        //   ...
        //
        // ※偶数チャネルの場合は折り返し点（両端重なりチャネル）はCFAR判定対象外。
        //
        // 判定結果はcfar_resultに格納。
        // ------------------------------------------------------
        for (size_t idx = 0; idx < sorted_len; ++idx) {
            size_t CUT = sorted_idx[idx];
            // TRAIN/GAURD領域の両側インデックス
            int left_start = (int)idx - CFAR_GUARD - CFAR_TRAIN;
            int left_end = (int)idx - CFAR_GUARD - 1;
            int right_start = (int)idx + CFAR_GUARD + 1;
            int right_end = (int)idx + CFAR_GUARD + CFAR_TRAIN;

            double train_sum = 0.0;
            int train_count = 0;

            // 左側TRAIN
            if (left_start >= 0) {
                for (int i = left_start; i <= left_end && i >= 0; ++i) {
                    // 左側の電力を加算
                    train_sum += power[sorted_idx[i]];
                    train_count++;
                }
            }
            // 右側TRAIN
            if (right_end < (int)sorted_len) {
                for (int i = right_start; i <= right_end && i < (int)sorted_len; ++i) {
                    // 右側の電力を加算
                    train_sum += power[sorted_idx[i]];
                    train_count++;
                }
            }
            // TRAINが片側しかない場合も対応
            double train_mean = (train_count > 0) ? (train_sum / train_count) : 0.0;
            // CFAR判定
            cfar_result[CUT] = (power[CUT] > CFAR_ALPHA * train_mean);

            // テスト出力
            if (cfar_result[CUT]) {
                // CUTが検出された場合の処理（例：ログ出力）
                printf("CFAR Detection: Channel %zu\n", CUT);
            }
        }

        // printf("%d\t%f\t%f\n", counter, power[0], power[1]);

        // counter++;
    }

    // Stop
    atomic_store(&running, false);

    // FFTWプランの破棄
    fftw_destroy_plan(plan);

    return NULL;
}

int channelizer_close(void) { return 0; }

// Polyphase Channelizer出力のチャネル順並び替え関数
// （channelizer_out の周波数配置は上記のコメントのようになっているので、周波数順に並び替えるための機能を定義）
void get_sorted_channel_indices(size_t num_channels, size_t *sorted_idx) {
    size_t N = num_channels / 2;
    size_t idx = 0;

    if (num_channels % 2 == 0) {
        // 偶数チャネル
        for (size_t i = N + 1; i < num_channels; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = 0;
        for (size_t i = 1; i < N; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = N - 1;
        // 折り返し点Nは除外
    } else {
        // 奇数チャネル
        for (size_t i = N + 1; i < num_channels; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = 0;
        for (size_t i = 1; i < N; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = N;
    }
}