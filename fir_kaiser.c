#include <math.h>
#include <stdlib.h>

// ベッセル関数の近似（カイザー窓用）
static double besselI0(double x) {
    double sum = 1.0;
    double y = x * x / 4.0;
    double t = 1.0;
    for (int k = 1; k < 25; ++k) {
        t *= y / (k * k);
        sum += t;
        if (t < 1e-10)
            break;
    }
    return sum;
}

// カイザー窓生成
void kaiser_window(double *w, int N, double beta) {
    double denom = besselI0(beta);
    for (int n = 0; n < N; ++n) {
        double ratio = 2.0 * n / (N - 1) - 1.0;
        w[n] = besselI0(beta * sqrt(1.0 - ratio * ratio)) / denom;
    }
}

// FIRフィルタ設計（低通）
// h: 出力係数配列（N要素）
// N: フィルタ長
// fc: 正規化カットオフ周波数（0.0〜0.5）
// beta: カイザー窓パラメータ
//
// betaの目安:
// 0.0: Rectangular window
// 5.0: Similar to a Hamming window
// 6.0: Similar to a Hann window
// 8.6: Similar to a Blackman window
void fir_design_kaiser_lowpass(double *h, int N, double fc, double beta) {
    double *w = (double *)malloc(N * sizeof(double));
    kaiser_window(w, N, beta);
    int M = N - 1;
    for (int n = 0; n < N; ++n) {
        double m = n - M / 2.0;
        // 理想低通FIR
        double sinc = (m == 0.0) ? 2.0 * fc : sin(2.0 * M_PI * fc * m) / (M_PI * m);
        h[n] = sinc * w[n];
    }
    free(w);
}

// 使用例:
// double h[N];
// fir_design_kaiser_lowpass(h, N, fc, beta);
