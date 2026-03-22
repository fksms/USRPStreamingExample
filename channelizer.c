#include <complex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fftw3.h>

#include "brb.h"
#include "burst_catcher.h"
#include "cfar.h"
#include "channelizer.h"
#include "fir_kaiser.h"
#include "lfrb.h"
#include "usrp.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// ---------------------Buffer---------------------
extern LockFreeRingBuffer lfrb;
extern BlockingRingBuffer brb;
// ------------------------------------------------

// チャネルの間隔をHz単位で計算して返す
double get_channel_spacing_hz(void) { return RX_SAMP_RATE / NUM_CHANNELS; }

// チャネライザのセットアップ
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
    handle->fftw.plan = fftw_plan_dft_1d(NUM_CHANNELS, handle->fftw.in, handle->fftw.out, FFTW_FORWARD, FFTW_MEASURE);
    channelizer_reset(handle);

    return 0;
}

// チャネライザで使用するレジスタの初期化
void channelizer_reset(channelizer_handle *handle) {
    memset(handle->reg, 0, sizeof(handle->reg));
    memset(handle->fftw.in, 0, sizeof(handle->fftw.in));
    memset(handle->fftw.out, 0, sizeof(handle->fftw.out));
}

/**
 * @brief ポリフェーズチャネライザの1ブロック処理を行う
 *
 * @param num_channels      分解するチャンネル数
 * @param time_slots        1チャネルあたりの時間スロット数
 * @param coef_per_stage    1チャネルあたりのFIRフィルタタップ数（可変）
 * @param reg               [num_channels][coef_per_stage] 各チャネルのFIRレジスタ配列
 * @param split_filter      [num_channels][coef_per_stage] 各チャネル用に分割されたFIR係数配列
 * @param fftw              FFTW入出力バッファ・プランをまとめた構造体へのポインタ
 * @param channelizer_in    [num_channels * time_slots] 入力複素信号配列
 * @param channelizer_out   [num_channels][time_slots] 出力複素信号配列（周波数チャネルごと）
 * @param power_per_channel [num_channels] 各チャネルの出力電力を格納する配列
 *
 * @return なし（void）
 */
void channelizer_process_block(int num_channels, int time_slots, int coef_per_stage,
                               double complex (*reg)[coef_per_stage], const double (*split_filter)[coef_per_stage],
                               fftw_handle *fftw, const double complex *channelizer_in, double complex *channelizer_out,
                               double *power_per_channel) {

    // チャネルごとの出力電力を初期化
    memset(power_per_channel, 0, sizeof(double) * num_channels);

    for (int nn = 0; nn < time_slots; ++nn) {
        // 各チャネルのレジスタを更新
        for (int ch = 0; ch < num_channels; ++ch) {
            for (int kk = coef_per_stage - 1; kk > 0; --kk) {
                reg[ch][kk] = reg[ch][kk - 1];
            }
            reg[ch][0] = channelizer_in[nn * num_channels + ch];
        }

        // FIR出力をFFT入力へ格納
        for (int ch = 0; ch < num_channels; ++ch) {
            double complex filter_output = 0.0;
            for (int kk = 0; kk < coef_per_stage; ++kk) {
                filter_output += reg[ch][kk] * split_filter[ch][kk];
            }
            fftw->in[ch] = filter_output;
        }

        // FFTを実行
        fftw_execute(fftw->plan);

        // チャネルごとに出力を保存し、電力を計算
        for (int ch = 0; ch < num_channels; ++ch) {
            ((double complex(*)[time_slots])channelizer_out)[ch][nn] = fftw->out[ch];
            double re = creal(fftw->out[ch]);
            double im = cimag(fftw->out[ch]);
            power_per_channel[ch] += re * re + im * im;
        }
    }

    // （出力配列 channelizer_out の周波数配置は以下を参照）
    //
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
}

// チャネライザのスレッド関数
void *channelizer_thread(void *arg) {
    // Channelizer handle
    channelizer_handle *handle = arg;

    // `RX_SAMP_RATE`と`INPUT_SAMPS`から待機時間を計算
    double wait_sec = (double)INPUT_SAMPS / RX_SAMP_RATE;
    time_t sec = (time_t)wait_sec;
    long nsec = (long)((wait_sec - sec) * 1e9);
    struct timespec ts = {sec, nsec};

    // リングバッファから読み取った信号を格納するためのバッファ
    static iq_sample_t output_buf[INPUT_SAMPS * 2];

    // 複素信号を格納するためのバッファ
    static double complex complex_signal[INPUT_SAMPS];

    // チャネライザ出力用バッファ
    static double complex channelizer_out[NUM_CHANNELS][TIME_SLOTS] = {0};

    // 1つ前のチャネライザ出力を保存するバッファ
    static double complex prev_channelizer_out[NUM_CHANNELS][TIME_SLOTS] = {0};

    // 各チャンネルの信号の電力を格納するバッファ
    double power[NUM_CHANNELS] = {0};

    // チャネル順並び替え用配列
    // （channelizer_out
    // の周波数配置は以下のコメントのようになっているので、周波数順に並び替えるためのインデックス配列を定義）
    int sorted_idx[NUM_CHANNELS];
    get_sorted_channel_indices(NUM_CHANNELS, sorted_idx);

    // CFARを実施するチャネル数
    // （偶数チャネルの場合は折り返し点を除外する）
    int sorted_len = get_valid_sorted_channel_count();

    // CFAR判定結果格納用
    bool cfar_result[NUM_CHANNELS] = {false};

    // 1つ前のCFAR判定結果を保存するバッファ
    bool prev_cfar_result[NUM_CHANNELS] = {false};

    // 各チャネルのBurstCatcher用配列
    static BurstCatcher burst_catcher[NUM_CHANNELS];

    while (atomic_load(&running)) {
        if (!lfrb_read(&lfrb, output_buf, INPUT_SAMPS * 2)) {
            // バッファ空の場合
            nanosleep(&ts, NULL);
            continue;
        }

        // I, Q を複素数に変換
        for (int j = 0; j < INPUT_SAMPS; ++j) {
            complex_signal[j] = output_buf[2 * j] + output_buf[2 * j + 1] * I;
        }

        // チャネライザ処理
        channelizer_process_block(NUM_CHANNELS, TIME_SLOTS, COEF_PER_STAGE, handle->reg, handle->split_filter,
                                  &handle->fftw, complex_signal, (double complex *)channelizer_out, power);

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
            // if (cfar_result[CUT]) {
            //     // CUTが検出された場合の処理（例：ログ出力）
            //     printf("CFAR Detection: Channel %d\n", CUT);
            // }
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

            // false→true: 1つ前フレームを先頭に、現在のフレームをその後ろに格納
            if (!prv && cur) {
                memcpy(&catcher->buf[0], prev_channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                memcpy(&catcher->buf[TIME_SLOTS], channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                catcher->len = 2 * TIME_SLOTS;
                catcher->active = true;
            }
            // true→true: 現在のフレームを追記
            else if (prv && cur) {
                if (catcher->len < MAX_FRAMES * TIME_SLOTS) {
                    memcpy(&catcher->buf[catcher->len], channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                    catcher->len += TIME_SLOTS;
                }

            }
            // true→false: 現在のフレームを追記して確定
            else if (prv && !cur) {
                if (catcher->len < MAX_FRAMES * TIME_SLOTS) {
                    memcpy(&catcher->buf[catcher->len], channelizer_out[ch], sizeof(double complex) * TIME_SLOTS);
                    catcher->len += TIME_SLOTS;
                }
                // 可変長配列を作成し、catcher->bufからコピー
                double complex *burst = malloc(sizeof(double complex) * catcher->len);
                if (burst) {
                    memcpy(burst, catcher->buf, sizeof(double complex) * catcher->len);
                    printf("Burst Catcher: Channel %d\n", ch);
                    // 配列のポインタと長さをリングバッファに書き込む
                    // （リーダー側でメモリ解放を忘れないこと！！）
                    brb_write(&brb, burst, catcher->len);
                } else {
                    printf("Burst Catcher: Channel %d, malloc failed\n", ch);
                    // メモリ確保に失敗した場合は強制終了
                    atomic_store(&running, false);
                    exit(EXIT_FAILURE);
                }
                catcher->len = 0;
                catcher->active = false;
            }
            // false→false: 何もしない
        }

        // 1つ前のCFAR結果とチャネライザ出力を保存
        memcpy(prev_cfar_result, cfar_result, sizeof(cfar_result));
        memcpy(prev_channelizer_out, channelizer_out, sizeof(channelizer_out));
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}

// チャネライザのクリーンアップ
int channelizer_close(channelizer_handle *handle) {
    // FFTWプランの破棄
    fftw_destroy_plan(handle->fftw.plan);

    return 0;
}

// `NUM_CHANNELS`が偶数（2N）の場合、Nチャネル目が折り返し点となり、
// 両端が重複するため、実際に使用できるチャネル数を返す
int get_valid_sorted_channel_count(void) { return (NUM_CHANNELS % 2 == 0) ? (NUM_CHANNELS - 1) : NUM_CHANNELS; }

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