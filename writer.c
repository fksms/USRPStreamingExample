#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "usrp.h"
#include "writer.h"

// #define TX_SAMP_RATE 500e3
// #define TX_NUM_SAMPS 1000

// ランダムデータ生成
void generate_bits(uint8_t *bits, int len) {
    srand((unsigned)time(NULL));

    // プリアンブルを定義
    const int preamble = 0x55; /* 01010101b */
    const int preamble_len = 8;

    if (len < preamble_len) {
        fprintf(stderr, "配列長がプリアンブルより短いです\n");
        return;
    }

    // プリアンブルをビット列に展開
    for (int i = 0; i < preamble_len; i++)
        bits[i] = (preamble >> (7 - i)) & 1;

    // ランダムビットを生成
    for (int i = preamble_len; i < len; i++)
        bits[i] = rand() & 1;

    /* 表示 */
    printf("入力データ: \n");
    for (int i = 0; i < len; i++) {
        printf("%u", bits[i]);
        if ((i + 1) % 8 == 0)
            printf(" ");
    }
    printf("\n");
}

/**
 * ガウスフィルタ係数の構築
 *
 * gauss_coef: フィルタ係数配列（呼び出し元で確保済み）
 * gauss_len : 配列長（GAUSS_SPAN×SPS+1）
 *
 * h(t) = (1/(√(2π)σ)) * exp(-t²/(2σ²))   [シンボル単位]
 * σ    = √(ln2) / (2π × BT)
 *
 * 配列の中心を0とし、両端まで計算。面積を1に正規化。
 */
void build_gaussian_filter(double *gauss_coef, int gauss_len) {
    // サンプル/シンボルを計算
    int sps = TX_SAMP_RATE / SYMBOL_RATE;
    // ガウス分布の標準偏差σを計算
    double sigma = sqrt(log(2.0)) / (2.0 * M_PI * BT);
    // 配列の中心インデックス
    int half = (GAUSS_SPAN * sps) / 2;
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
}

/* ================================================================
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
 * ================================================================ */
int fsk_modulate(const uint8_t *bits, int n_bits, double *gauss_coef, int gauss_len, float *iq_out, int *n_samples, bool use_gaussian) {
    // サンプル/シンボル数を計算
    int sps = TX_SAMP_RATE / SYMBOL_RATE; /* サンプル/シンボル */
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
    for (int i = 0; i < n_bits; i++) {
        up[i * sps] = (bits[i] == 1) ? 1.0 : -1.0;
    }

    double *gf = NULL;
    int out = raw_len;
    double *g = NULL;
    if (use_gaussian) {
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
        // 位相増分: π×h×g[n]
        phase += M_PI * MOD_INDEX * g[n];
        // 位相ラップ（数値安定化）
        while (phase > M_PI)
            phase -= 2.0 * M_PI;
        while (phase < -M_PI)
            phase += 2.0 * M_PI;

        // IQ信号生成
        iq_out[2 * n] = (float)cos(phase);     // I成分
        iq_out[2 * n + 1] = (float)sin(phase); // Q成分
    }

    if (gf)
        free(gf);
    free(up);
    *n_samples = out;
    return 0;
}

/* ================================================================
 * IQ データをファイルに保存
 *   フォーマット: float32 interleaved  [I0,Q0, I1,Q1, ...]
 * ================================================================ */
void write_iq_file(const char *path, const float *iq, int len) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("fopen");
        return;
    }
    // CSV形式で出力: I,Q\n
    for (int i = 0; i < len / 2; i++) {
        fprintf(fp, "%f,%f\n", iq[2 * i], iq[2 * i + 1]);
    }
    fclose(fp);
    printf("[File] %s  (%d samples, %.1f μs)\n", path, len / 2, (double)len / TX_SAMP_RATE / 2 * 1e6);
}

// int main() {
//     // 送信するビット列
//     int n_bits = 250;
//     uint8_t bits[n_bits];
//     generate_bits(bits, n_bits);

//     // ガウスフィルタ係数の構築
//     int gauss_len = GAUSS_SPAN * (TX_SAMP_RATE / SYMBOL_RATE) + 1;
//     double gauss_coef[gauss_len];
//     build_gaussian_filter(gauss_coef, gauss_len);

//     // FSK/GFSK変調
//     int n_samples;
//     int sps = TX_SAMP_RATE / SYMBOL_RATE;
//     // (n_bits * sps)がTX_NUM_SAMPSの倍数になるように切り上げ
//     int iq_len_raw = 2 * n_bits * sps;
//     int iq_block = 2 * TX_NUM_SAMPS;
//     int iq_len = ((iq_len_raw + iq_block - 1) / iq_block) * iq_block; // 切り上げ

//     // 信号格納用
//     float iq[iq_len];
//     if (fsk_modulate(bits, n_bits, gauss_coef, gauss_len, iq, &n_samples, true) != 0) {
//         fprintf(stderr, "変調に失敗しました\n");
//         return EXIT_FAILURE;
//     }
//     // iq_lenの長さがn_samplesより長い場合は残りを0で埋める
//     for (int i = 2 * n_samples; i < iq_len; i++) {
//         iq[i] = 0.0f;
//     }

//     // IQデータをファイルに保存
//     write_iq_file("fsk_iq.csv", iq, iq_len);

//     // float配列iqをint16_t配列に変換
//     int16_t iq_int[iq_len];
//     for (int i = 0; i < iq_len; i++) {
//         // 1.0→32767, -1.0→-32768（クリッピング）
//         float val = iq[i];
//         if (val > 1.0f)
//             val = 1.0f;
//         if (val < -1.0f)
//             val = -1.0f;
//         iq_int[i] = (int16_t)(val * INT16_MAX);
//     }

//     return EXIT_SUCCESS;
// }