#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "writer.h"

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
 * アルゴリズム:
 *   1. NRZ変換      : 0 → -1,  1 → +1
 *   2. アップサンプル: SPS倍（ゼロ挿入）
 *   3. ガウス畳み込み: 帯域制限された周波数パルス g(t) を得る（オプション）
 *   4. 位相積分      : φ[n] = φ[n-1] + π×h×g[n]
 *   5. IQ 生成       : I=cos(φ),  Q=sin(φ)
 *
 * 返値:
 *   *iq_out      : float32 interleaved IQ  [I0,Q0, I1,Q1, ...]
 *   *n_samples   : サンプル数
 *   戻り値       : 0=成功, -1=失敗
 */
int fsk_modulate_at_rate(const uint8_t *bits, int n_bits, double sample_rate_hz, const double *gauss_coef, int gauss_len, double complex *iq_out, int *n_samples, bool use_gaussian) {
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
 * アルゴリズム:
 *   1. 位相差計算: φ[n] = atan2(Q[n], I[n])
 *   2. 差分計算: Δφ[n] = φ[n] - φ[n-1]
 *   3. ガウスフィルタ畳み込み: 帯域制限された周波数パルス g(t) を得る（オプション）
 *   4. シンボル判定: Δφ[n] の符号でビットを決定
 *
 * 返値:
 *   *bits_out    : 復調ビット列
 *   *n_bits_out  : 復調ビット数
 *   戻り値       : 0=成功, -1=失敗
 */
int fsk_demodulate_at_rate(const double complex *iq_in, int n_samples, double sample_rate_hz, const double *gauss_coef, int gauss_len, uint8_t *bits_out, int max_bits, bool use_gaussian,
                           int *n_bits_out) {
    int sps = get_samples_per_symbol(sample_rate_hz);
    if (sps < 0 || n_samples < 2 || max_bits <= 0)
        return -1;

    int discr_len = n_samples - 1;
    double *discr = calloc((size_t)discr_len, sizeof(double));
    if (!discr)
        return -1;

    for (int n = 1; n < n_samples; ++n) {
        double complex diff = conj(iq_in[n - 1]) * iq_in[n];
        discr[n - 1] = atan2(cimag(diff), creal(diff));
    }

    double *work = discr;
    double *filtered = NULL;
    if (use_gaussian) {
        if (!gauss_coef || gauss_len <= 0) {
            free(discr);
            return -1;
        }

        filtered = calloc((size_t)discr_len, sizeof(double));
        if (!filtered) {
            free(discr);
            return -1;
        }

        for (int n = 0; n < discr_len; ++n) {
            double acc = 0.0;
            for (int k = 0; k < gauss_len; ++k) {
                int idx = n - k;
                if (idx >= 0 && idx < discr_len)
                    acc += gauss_coef[k] * discr[idx];
            }
            filtered[n] = acc;
        }
        work = filtered;
    }

    int best_offset = 0;
    double best_score = -1.0;

    for (int offset = 0; offset < sps; ++offset) {
        int symbol_count = (discr_len - offset) / sps;
        double score = 0.0;

        for (int sym = 0; sym < symbol_count; ++sym) {
            double sum = 0.0;
            int start = offset + sym * sps;
            for (int j = 0; j < sps; ++j)
                sum += work[start + j];
            score += fabs(sum);
        }

        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }

    int decoded_bits = (discr_len - best_offset) / sps;
    if (decoded_bits > max_bits)
        decoded_bits = max_bits;

    for (int sym = 0; sym < decoded_bits; ++sym) {
        double sum = 0.0;
        int start = best_offset + sym * sps;
        for (int j = 0; j < sps; ++j)
            sum += work[start + j];
        bits_out[sym] = (sum >= 0.0) ? 1 : 0;
    }

    *n_bits_out = decoded_bits;

    free(filtered);
    free(discr);
    return 0;
}