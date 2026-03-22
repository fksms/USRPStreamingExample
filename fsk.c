#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fsk.h"

// サンプルレートからシンボルあたりのサンプル数（SPS）を計算して返す
int get_samples_per_symbol(double sample_rate_hz) {
    double sps_exact = sample_rate_hz / SYMBOL_RATE;
    int sps = (int)lround(sps_exact);

    if (sps <= 1 || fabs(sps_exact - sps) > 1e-9)
        return -1;

    return sps;
}

// ガウスフィルタの長さをサンプルレートに基づいて計算して返す
int get_gaussian_filter_length(double sample_rate_hz) {
    int sps = get_samples_per_symbol(sample_rate_hz);
    if (sps < 0)
        return -1;

    return GAUSS_SPAN * sps + 1;
}

/**
 * ガウスフィルタ係数の構築
 *
 * sample_rate_hz: サンプルレート（Hz）
 * gauss_coef: フィルタ係数配列（呼び出し元で確保済み）
 * gauss_len : 配列長（GAUSS_SPAN×SPS+1）
 *
 * h(t) = (1/(√(2π)σ)) * exp(-t²/(2σ²))   [シンボル単位]
 * σ    = √(ln2) / (2π × BT)
 *
 * 配列の中心を0とし、両端まで計算。面積を1に正規化。
 */
int build_gaussian_filter_for_rate(double sample_rate_hz, double *gauss_coef, int gauss_len) {
    // サンプル/シンボルを計算
    int sps = get_samples_per_symbol(sample_rate_hz);
    if (sps < 0)
        return -1;

    int expected_len = get_gaussian_filter_length(sample_rate_hz);
    if (gauss_len != expected_len)
        return -1;

    // ガウス分布の標準偏差σを計算
    double sigma = sqrt(log(2.0)) / (2.0 * M_PI * BT);
    // 配列の中心インデックス
    int half = (get_gaussian_filter_length(sample_rate_hz) - 1) / 2;
    double sum = 0.0;
    // ガウス係数計算
    for (int i = 0; i < gauss_len; i++) {
        double t = (double)(i - half) / sps; // シンボル単位で中心を0に
        gauss_coef[i] = exp(-t * t / (2.0 * sigma * sigma));
        sum += gauss_coef[i];
    }
    // 正規化（合計を1に）
    for (int i = 0; i < gauss_len; i++)
        gauss_coef[i] /= sum; /* 正規化: Σ = 1 */

    return 0;
}

/**
 * FSK変調（ガウスフィルタ適用可）
 *
 * @param {const uint8_t*} bits        変調するビット列（0または1の配列）
 * @param {int} n_bits                 ビット数
 * @param {double} sample_rate_hz      サンプルレート（Hz）
 * @param {bool} use_gaussian          ガウスフィルタを適用する場合true（GFSK）、しない場合false（FSK）
 * @param {const double*} gauss_coef   ガウスフィルタ係数配列（use_gaussian=true時のみ使用）
 * @param {int} gauss_len              ガウスフィルタ係数配列の長さ
 * @param {double complex*} iq_out     出力IQ信号配列（float32 interleaved: I0,Q0, I1,Q1,...）
 * @param {int*} n_samples             生成されるサンプル数（出力）
 *
 * @return {int} 0:成功、-1:失敗（引数不正やメモリ確保失敗時）
 *
 * @algorithm
 *   1. NRZ変換      : 入力ビット列をNRZ（0→-1, 1→+1）に変換
 *   2. アップサンプル : シンボルあたりSPS倍に拡張（サンプルホールド）
 *   3. ガウス畳み込み : ガウスフィルタで帯域制限（use_gaussian=true時のみ）
 *   4. 位相積分      : φ[n] = φ[n-1] + π×h×g[n] で位相系列を生成
 *   5. IQ生成       : I=cos(φ), Q=sin(φ) でIQ信号を生成
 *
 * @note
 *   - gauss_coef, gauss_lenはuse_gaussian=true時のみ有効
 *   - iq_out, n_samplesは呼び出し元で十分な領域を確保すること
 */
int fsk_modulate_at_rate(const uint8_t *bits, int n_bits, double sample_rate_hz, bool use_gaussian,
                         const double *gauss_coef, int gauss_len, double complex *iq_out, int *n_samples) {
    // サンプル/シンボル数を計算
    int sps = get_samples_per_symbol(sample_rate_hz);
    if (sps < 0)
        return -1;

    // アップサンプル後の長さ
    int raw_len = n_bits * sps;
    // 畳み込み後の長さ
    int filt_len = raw_len + gauss_len - 1;
    // フィルタ群遅延（中心位置）
    int delay = gauss_len / 2;

    // --- NRZ変換＆アップサンプル ---
    // NRZ: 0→-1, 1→+1。アップサンプル（ゼロ挿入）
    double *up = (double *)calloc(filt_len, sizeof(double));
    if (!up)
        return -1;
    // サンプルホールド + 1/sps スケーリング
    for (int i = 0; i < n_bits; i++) {
        double nrz = (bits[i] == 1) ? 1.0 : -1.0;
        for (int j = 0; j < sps; j++)
            up[i * sps + j] = nrz; // sps回繰り返す
    }

    double *gf = NULL;
    int out = raw_len;
    double *g = NULL;
    if (use_gaussian) {
        if (!gauss_coef || gauss_len <= 0)
            return -1;
        // --- ガウスフィルタ畳み込み ---
        gf = (double *)calloc(filt_len, sizeof(double));
        if (!gf) {
            free(up);
            return -1;
        }
        for (int n = 0; n < filt_len; n++) {
            double acc = 0.0;
            for (int k = 0; k < gauss_len; k++) {
                int idx = n - k;
                if (idx >= 0 && idx < raw_len)
                    acc += gauss_coef[k] * up[idx];
            }
            gf[n] = acc;
        }
        g = gf + delay; // 群遅延補正
    } else {
        // フィルタなし: up配列をそのまま使う
        g = up;
    }
    // --- 位相積分とIQ生成 ---
    double phase = 0.0;
    for (int n = 0; n < out; n++) {
        // 位相増分: π×h×g[n]（1/sps スケーリング）
        phase += M_PI * MOD_INDEX / sps * g[n];
        // 位相ラップ（数値安定化）
        while (phase > M_PI)
            phase -= 2.0 * M_PI;
        while (phase < -M_PI)
            phase += 2.0 * M_PI;

        // IQ信号生成
        iq_out[n] = cos(phase) + sin(phase) * I;
    }

    if (gf)
        free(gf);
    free(up);
    *n_samples = out;
    return 0;
}

/**
 * FSK復調（ガウスフィルタ適用可）
 *
 * @param {const double complex*} iq_in   入力IQ信号配列（float32 interleaved: I0,Q0, I1,Q1,...）
 * @param {int} n_samples                 入力サンプル数
 * @param {double} sample_rate_hz         サンプルレート（Hz）
 * @param {bool} use_gaussian             ガウスフィルタを適用する場合true（GFSK）、しない場合false（FSK）
 * @param {const double*} gauss_coef      ガウスフィルタ係数配列（use_gaussian=true時のみ使用）
 * @param {int} gauss_len                 ガウスフィルタ係数配列の長さ
 * @param {int} max_bits                  出力可能な最大ビット数（bits_out配列の最大長）
 * @param {uint8_t*} bits_out             復調ビット列（出力、0または1の配列）
 * @param {int*} n_bits_out               復調ビット数（出力）
 *
 * @return {int} 0:成功、-1:失敗（引数不正やメモリ確保失敗時）
 *
 * @algorithm
 *   1. IQ信号から隣接サンプル間の位相差（Δφ）を計算
 *   2. （オプション）ガウスフィルタで帯域制限（use_gaussian=true時のみ）
 *   3. シンボル境界の最適オフセットを探索
 *   4. シンボルごとにΔφの合計値の符号でビット判定
 *   5. 復調ビット列を出力
 *
 * @note
 *   - gauss_coef, gauss_lenはuse_gaussian=true時のみ有効
 *   - bits_out, n_bits_outは呼び出し元で十分な領域を確保すること
 */
int fsk_demodulate_at_rate(const double complex *iq_in, int n_samples, double sample_rate_hz, bool use_gaussian,
                           const double *gauss_coef, int gauss_len, int max_bits, uint8_t *bits_out, int *n_bits_out) {
    // サンプルレートからシンボルあたりのサンプル数（SPS）を計算
    int sps = get_samples_per_symbol(sample_rate_hz);
    if (sps <= 0 || n_samples < 2 || max_bits <= 0 || !n_bits_out)
        return -1;

    // 位相差系列の長さ（n_samples-1）
    int discr_len = n_samples - 1;
    // 位相差系列（周波数偏移系列）用バッファ確保
    double *discr = calloc((size_t)discr_len, sizeof(double));
    if (!discr)
        return -1;

    // IQ信号から隣接サンプル間の位相差（Δφ）を計算
    for (int n = 1; n < n_samples; ++n) {
        double complex diff = conj(iq_in[n - 1]) * iq_in[n];
        discr[n - 1] = atan2(cimag(diff), creal(diff));
    }

    double *work = discr;    // 復調処理用の作業配列
    double *filtered = NULL; // ガウスフィルタ後のバッファ
    int filter_delay = 0;    // フィルタ遅延（群遅延）

    // --- ガウスフィルタ適用（GFSK復調時） ---
    if (use_gaussian) {
        if (!gauss_coef || gauss_len <= 0) {
            free(discr);
            return -1;
        }

        // フィルタ後バッファ確保
        filtered = calloc((size_t)discr_len, sizeof(double));
        if (!filtered) {
            free(discr);
            return -1;
        }

        filter_delay = gauss_len / 2; // フィルタ遅延（中心位置）

        // 離散畳み込み（中心対称）
        for (int n = 0; n < discr_len; ++n) {
            double acc = 0.0;
            for (int k = 0; k < gauss_len; ++k) {
                int idx = n - k + filter_delay; // フィルタ中心合わせ
                if (idx >= 0 && idx < discr_len)
                    acc += gauss_coef[k] * discr[idx];
            }
            filtered[n] = acc;
        }
        work = filtered; // フィルタ後の系列を以降の処理に使う
    }

    // --- シンボル境界の最適オフセット探索 ---
    int best_offset = 0;
    double best_score = -DBL_MAX;

    // 0〜(SPS-1)の各オフセットでスコアを計算し、最も絶対値合計が大きいものを選ぶ
    for (int offset = 0; offset < sps; ++offset) {
        int symbol_count = (discr_len - filter_delay - offset) / sps;
        if (symbol_count <= 0)
            continue;
        double score = 0.0;

        // 各シンボル区間ごとにSPSサンプルを合計し、その絶対値をスコアに加算
        for (int sym = 0; sym < symbol_count; ++sym) {
            double sum = 0.0;
            int start = filter_delay + offset + sym * sps;
            for (int j = 0; j < sps; ++j)
                sum += work[start + j];
            score += fabs(sum);
        }

        // 最良スコア・オフセットを記録
        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }

    // --- 復調ビット数の決定 ---
    int decoded_bits = (discr_len - filter_delay - best_offset) / sps;
    if (decoded_bits > max_bits)
        decoded_bits = max_bits;
    if (decoded_bits < 0)
        decoded_bits = 0;

    // --- シンボルごとにビット判定 ---
    for (int sym = 0; sym < decoded_bits; ++sym) {
        double sum = 0.0;
        int start = filter_delay + best_offset + sym * sps;
        for (int j = 0; j < sps; ++j)
            sum += work[start + j];
        // 合計値の符号でビット判定（0:負→0, 正→1）
        bits_out[sym] = (sum >= 0.0) ? 1 : 0;
    }

    *n_bits_out = decoded_bits; // 出力ビット数を返す

    // バッファ解放
    free(filtered);
    free(discr);
    return 0;
}