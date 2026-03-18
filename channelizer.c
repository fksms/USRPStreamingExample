#include <complex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <fftw3.h>

#include "burst_catcher.h"
#include "cfar.h"
#include "channelizer.h"
#include "fir_kaiser.h"
#include "lfrb.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern double rx_rate;

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
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        for (int j = 0; j < COEF_PER_STAGE; ++j) {
            split_filter[ch][j] = filter_coef[j * NUM_CHANNELS + ch];
        }
    }

    // FFTWプランの作成
    handle->plan = fftw_plan_dft_1d(NUM_CHANNELS, handle->in, handle->out, FFTW_FORWARD, FFTW_MEASURE);

    return 0;
}

void *channelizer_thread(void *arg) {
    // Channelizer handle
    channelizer_handle *handle = arg;

    // `rx_rate`と`OUTPUT_SAMPS`から待機時間を計算
    double wait_sec = (double)OUTPUT_SAMPS / rx_rate;
    time_t sec = (time_t)wait_sec;
    long nsec = (long)((wait_sec - sec) * 1e9);
    struct timespec ts = {sec, nsec};

    // リングバッファから読み取った信号を格納するためのバッファ
    static iq_sample_t output_buf[OUTPUT_ELEMS];

    // 複素信号を格納するためのバッファ
    static double complex complex_signal[OUTPUT_SAMPS];

    // 分割されたFIRフィルタ係数へのポインタ
    double(*split_filter)[COEF_PER_STAGE] = handle->split_filter;

    // 畳み込み用レジスタ
    static double complex reg[NUM_CHANNELS][COEF_PER_STAGE] = {0};

    // チャネライザ出力用バッファ
    static double complex channelizer_out[NUM_CHANNELS][TIME_SLOTS] = {0};

    // 1つ前のチャネライザ出力を保存するバッファ
    static double complex prev_channelizer_out[NUM_CHANNELS][TIME_SLOTS] = {0};

    // 各チャンネルの信号の電力を格納するバッファ
    double power[NUM_CHANNELS] = {0};

    // チャネル順並び替え用配列
    // （channelizer_out の周波数配置は以下のコメントのようになっているので、周波数順に並び替えるためのインデックス配列を定義）
    int sorted_idx[NUM_CHANNELS];
    get_sorted_channel_indices(NUM_CHANNELS, sorted_idx);

    // CFARを実施するチャネル数
    // （偶数チャネルの場合は折り返し点を除外する）
    int sorted_len = NUM_CHANNELS;
    if (NUM_CHANNELS % 2 == 0)
        sorted_len--; // 偶数時は折り返し点除外

    // CFAR判定結果格納用
    bool cfar_result[NUM_CHANNELS] = {false};

    // 1つ前のCFAR判定結果を保存するバッファ
    bool prev_cfar_result[NUM_CHANNELS] = {false};

    // 各チャネルのBurstCatcher用配列
    static BurstCatcher burst_catcher[NUM_CHANNELS];

    while (atomic_load(&running)) {
        if (!lfrb_read(&rb, output_buf)) {
            // バッファ空の場合
            nanosleep(&ts, NULL);
            continue;
        }

        // I, Q を複素数に変換
        for (int j = 0; j < OUTPUT_SAMPS; ++j) {
            complex_signal[j] = output_buf[2 * j] + output_buf[2 * j + 1] * I;
        }

        // 信号電力を初期化
        memset(power, 0, sizeof(power));

        // ----------------------- Channelizer -----------------------
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
        for (int nn = 0; nn < TIME_SLOTS; ++nn) {

            // 各チャネルのレジスタを更新
            for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
                // レジスタを右向きに1つシフト
                for (int kk = 1; kk < COEF_PER_STAGE; ++kk) {
                    reg[ch][kk] = reg[ch][kk - 1];
                }
                // レジスタの最左列に信号を1つ代入
                reg[ch][0] = complex_signal[nn * NUM_CHANNELS + ch];
            }

            // 時間信号とフィルタの畳み込みを行った後、FFTWの入力配列 in に格納
            for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
                // フィルタ出力を計算
                double complex filter_output = 0;
                // reg[ch, ::-1]とsplit_filter[ch, :]の内積
                for (int kk = 0; kk < COEF_PER_STAGE; ++kk) {
                    filter_output += reg[ch][COEF_PER_STAGE - kk - 1] * split_filter[ch][kk];
                }
                handle->in[ch] = filter_output;
            }

            // FFTWを用いてIFFTを実行
            fftw_execute(handle->plan);

            // IFFT結果をchannelizer_outに格納
            // （信号の電力も同時に計算する）
            for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
                channelizer_out[ch][nn] = handle->out[ch];
                double re = creal(handle->out[ch]);
                double im = cimag(handle->out[ch]);
                power[ch] += re * re + im * im;
            }
        }

        // -------------------------- CFAR ---------------------------
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
        // -----------------------------------------------------------
        for (int idx = 0; idx < sorted_len; ++idx) {
            int CUT = sorted_idx[idx];
            // TRAIN/GAURD領域の両側インデックス
            int left_start = idx - CFAR_GUARD - CFAR_TRAIN;
            int left_end = idx - CFAR_GUARD - 1;
            int right_start = idx + CFAR_GUARD + 1;
            int right_end = idx + CFAR_GUARD + CFAR_TRAIN;

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
            if (right_end < sorted_len) {
                for (int i = right_start; i <= right_end && i < sorted_len; ++i) {
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
                printf("CFAR Detection: Channel %d\n", CUT);
            }
        }

        // ---------------------- Burst Catcher ----------------------
        // 概要：
        // Burst Catcherは、CFAR判定結果に基づき、バースト信号（連続した検出区間）の開始・継続・終了を管理する。
        // 具体的には、各チャネルごとに以下の状態遷移でバースト信号をバッファに蓄積する：
        //   - false→true: バースト開始。前フレームと現フレームをバッファに積む
        //   - true→true: バースト継続。現フレームをバッファに追記
        //   - true→false: バースト終了。現フレームまで追記し、バッファを確定（flush等）
        //   - false→false: 何もしない
        // バッファには最大MAX_FRAMESまでフレームを蓄積し、lenで現在の蓄積数を管理。
        // activeフラグでバースト中かどうかを判定。
        // バースト終了時には、バッファ内容を処理（flush_burst等）し、lenとactiveをリセットする。
        // -----------------------------------------------------------
        for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
            // バッファへのポインタ
            BurstCatcher *catcher = &burst_catcher[ch];

            // CFAR判定の現在と1つ前の状態を比較して、バーストの開始・継続・終了を判断
            bool cur = cfar_result[ch];
            bool prv = prev_cfar_result[ch];

            if (!prv && cur) {
                // false→true: 1つ前フレームを先頭に、現フレームを2番目に積む
                memcpy(catcher->buf[0], prev_channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                memcpy(catcher->buf[1], channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                catcher->len = 2;
                catcher->active = true;

            } else if (prv && cur) {
                // true→true: 現フレームを追記
                if (catcher->len < MAX_FRAMES) {
                    memcpy(catcher->buf[catcher->len++], channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                }

            } else if (prv && !cur) {
                // true→false: 現フレームまで追記して確定
                if (catcher->len < MAX_FRAMES) {
                    memcpy(catcher->buf[catcher->len++], channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                }
                // flush_burst(ch, catcher->buf, catcher->len);
                printf("Burst Catcher: Channel %d, Length %d frames\n", ch, catcher->len);
                catcher->len = 0;
                catcher->active = false;
            }
            // false→false (!prv && !cur): 何もしない
        }

        // 1つ前のCFAR結果とチャネライザ出力を保存
        memcpy(prev_cfar_result, cfar_result, sizeof(cfar_result));
        memcpy(prev_channelizer_out, channelizer_out, sizeof(channelizer_out));
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}

int channelizer_close(channelizer_handle *handle) {
    // FFTWプランの破棄
    fftw_destroy_plan(handle->plan);

    return 0;
}

// Polyphase Channelizer出力のチャネル順並び替え関数
// （channelizer_out の周波数配置は上記のコメントのようになっているので、周波数順に並び替えるための機能を定義）
void get_sorted_channel_indices(int num_channels, int *sorted_idx) {
    int N = num_channels / 2;
    int idx = 0;

    for (int i = N + 1; i < num_channels; ++i)
        sorted_idx[idx++] = i;
    for (int i = 0; i < N + 1; ++i)
        sorted_idx[idx++] = i;
}