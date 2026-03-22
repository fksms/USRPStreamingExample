#include <complex.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fftw3.h>

#include "channelizer.h"
#include "channelizer_test.h"
#include "fsk.h"
#include "usrp.h"

// セルフテスト用の周波数を計算して返す
static double get_self_test_frequency_hz(int sorted_position, int sorted_len) {
    int center = sorted_len / 2;
    return (sorted_position - center) * get_channel_spacing_hz();
}

// テスト用の単一トーン信号を生成してcomplex_signalに格納する
static void fill_tone_block(double complex *complex_signal, int len, double tone_hz, double sample_rate_hz) {
    const double amplitude = 0.8;
    const double two_pi = 2 * M_PI;

    for (int n = 0; n < len; ++n) {
        double phase = two_pi * tone_hz * (double)n / sample_rate_hz;
        complex_signal[n] = amplitude * (cos(phase) + sin(phase) * I);
    }
}

// チャネライザのセルフテストを実行する
//
// テスト内容：
//   50チャネルのチャネライザに対して、中心周波数がチャネルの中心に位置する単一トーン信号を入力し、最も強いチャネルが正しいかどうかを評価する
int channelizer_run_self_test(channelizer_handle *handle, FILE *stream) {
    static double complex complex_signal[INPUT_SAMPS];
    static double complex channelizer_out[NUM_CHANNELS][TIME_SLOTS];
    int sorted_idx[NUM_CHANNELS];
    int sorted_len = get_valid_sorted_channel_count();
    int failures = 0;

    get_sorted_channel_indices(NUM_CHANNELS, sorted_idx);

    fprintf(stream, "Channelizer self-test start\n");
    fprintf(stream, "  sample_rate = %.0f Hz\n", RX_SAMP_RATE);
    fprintf(stream, "  num_channels = %d\n", NUM_CHANNELS);
    fprintf(stream, "  channel_spacing = %.0f Hz\n", get_channel_spacing_hz());

    for (int idx = 0; idx < sorted_len; ++idx) {
        double power[NUM_CHANNELS];
        double tone_hz = get_self_test_frequency_hz(idx, sorted_len);
        int expected_channel = sorted_idx[idx];
        int detected_channel = -1;
        int second_channel = -1;
        double max_power = -1.0;
        double second_power = -1.0;

        fill_tone_block(complex_signal, INPUT_SAMPS, tone_hz, RX_SAMP_RATE);
        channelizer_reset(handle);

        // 最後の1ブロックを評価して初期過渡の影響を減らす
        for (int warmup = 0; warmup < 3; ++warmup) {
            channelizer_process_block(NUM_CHANNELS, TIME_SLOTS, COEF_PER_STAGE, handle->reg, handle->split_filter,
                                      &handle->fftw, complex_signal, (double complex *)channelizer_out, power);
        }

        for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
            if (power[ch] > max_power) {
                second_power = max_power;
                second_channel = detected_channel;
                max_power = power[ch];
                detected_channel = ch;
            } else if (power[ch] > second_power) {
                second_power = power[ch];
                second_channel = ch;
            }
        }

        bool passed = (detected_channel == expected_channel);

        fprintf(stream,
                "  [%s] tone=%+.0f Hz expected_ch=%d detected_ch=%d main_power=%.3e next_ch=%d next_power=%.3e\n",
                passed ? "PASS" : "FAIL", tone_hz, expected_channel, detected_channel, max_power, second_channel,
                second_power);

        if (!passed) {
            failures++;
        }
    }

    if (failures == 0) {
        fprintf(stream, "Channelizer self-test passed (%d/%d)\n", sorted_len, sorted_len);
        return 0;
    }

    fprintf(stream, "Channelizer self-test failed (%d/%d failed)\n", failures, sorted_len);
    return -1;
}

// プリアンブルのビット列を生成する関数
static uint8_t get_preamble_bit(int index) {
    const uint8_t preamble = 0x55; /* 01010101b */
    return (preamble >> (7 - index)) & 1;
}

// ランダムデータ生成
void generate_bits(uint8_t *bits, int len) {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = true;
    }

    // プリアンブルを定義
    if (len < PREAMBLE_LEN) {
        fprintf(stderr, "配列長がプリアンブルより短いです\n");
        return;
    }

    // プリアンブルをビット列に展開
    for (int i = 0; i < PREAMBLE_LEN; i++)
        bits[i] = get_preamble_bit(i);

    // ランダムビットを生成
    for (int i = PREAMBLE_LEN; i < len; i++)
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

// チャネル番号から中心周波数をHz単位で計算して返す
static bool get_channel_center_hz(int channel, double *freq_hz) {
    int half = NUM_CHANNELS / 2;
    double spacing = get_channel_spacing_hz();

    if (channel < 0 || channel >= NUM_CHANNELS)
        return false;

    if ((NUM_CHANNELS % 2 == 0) && channel == half)
        return false;

    if (channel == 0) {
        *freq_hz = 0.0;
    } else if (channel <= half) {
        *freq_hz = channel * spacing;
    } else {
        *freq_hz = (channel - NUM_CHANNELS) * spacing;
    }

    return true;
}

// 電力スペクトルから最も強いチャネルを見つけ、そのチャネル番号を返す
static int find_strongest_channel(const double power[NUM_CHANNELS], int *second_channel, double *second_power) {
    int detected_channel = -1;
    double max_power = -1.0;
    *second_channel = -1;
    *second_power = -1.0;

    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        if (power[ch] > max_power) {
            *second_power = max_power;
            *second_channel = detected_channel;
            max_power = power[ch];
            detected_channel = ch;
        } else if (power[ch] > *second_power) {
            *second_power = power[ch];
            *second_channel = ch;
        }
    }

    return detected_channel;
}

// 送信ビット列と受信ビット列を、最もエラーが少なくなるようにシフトして重ね合わせ、エラー数と重なりサンプル数を返す
static int align_bits_and_count_errors(const uint8_t *tx_bits, int tx_len, const uint8_t *rx_bits, int rx_len,
                                       int *best_shift, int *best_overlap) {
    int min_errors = tx_len + rx_len + 1;
    int chosen_shift = 0;
    int chosen_overlap = 0;

    for (int shift = -rx_len; shift <= tx_len; ++shift) {
        int errors = 0;
        int overlap = 0;

        for (int rx_idx = 0; rx_idx < rx_len; ++rx_idx) {
            int tx_idx = rx_idx + shift;
            if (tx_idx < 0 || tx_idx >= tx_len)
                continue;
            errors += (tx_bits[tx_idx] != rx_bits[rx_idx]);
            overlap++;
        }

        if (overlap == 0)
            continue;

        if (errors < min_errors || (errors == min_errors && overlap > chosen_overlap)) {
            min_errors = errors;
            chosen_shift = shift;
            chosen_overlap = overlap;
        }
    }

    *best_shift = chosen_shift;
    *best_overlap = chosen_overlap;
    return min_errors;
}

// 送信側サンプル列を受信側サンプルレートへ線形補間で変換する
static int resample_complex_linear(const double complex *src, int src_len, double src_rate_hz, double dst_rate_hz,
                                   double complex *dst, int dst_capacity) {
    if (src_len <= 1 || src_rate_hz <= 0.0 || dst_rate_hz <= 0.0)
        return -1;

    double ratio = dst_rate_hz / src_rate_hz;
    int dst_len = (int)lround((double)src_len * ratio);
    if (dst_len <= 1 || dst_len > dst_capacity)
        return -1;

    for (int n = 0; n < dst_len; ++n) {
        double src_pos = (double)n / ratio;
        int idx = (int)floor(src_pos);
        double frac = src_pos - idx;

        if (idx < 0)
            idx = 0;
        if (idx >= src_len - 1) {
            dst[n] = src[src_len - 1];
            continue;
        }

        dst[n] = src[idx] * (1.0 - frac) + src[idx + 1] * frac;
    }

    return dst_len;
}

// 単一トーン信号を指定した搬送波周波数で変調し、チャネライザ入力用の複素信号を生成する
static void build_shifted_modulated_block(const double complex *baseband, int n_samples, double src_rate_hz,
                                          double carrier_hz, double complex *mixed_signal, int signal_capacity) {
    const double two_pi = 2 * M_PI;

    memset(mixed_signal, 0, sizeof(double complex) * signal_capacity);

    // 搬送波を乗算して周波数シフト
    for (int n = 0; n < n_samples && n < signal_capacity; ++n) {
        int sample_index = n;
        double phase = two_pi * carrier_hz * (double)sample_index / src_rate_hz;
        double complex mixer = cos(phase) + sin(phase) * I;
        mixed_signal[sample_index] = baseband[n] * mixer;
    }
}

// チャネライザのモデムループバックテストを実行する
int channelizer_run_modem_loopback_test(channelizer_handle *handle, int channel, FILE *stream) {
    // 各SPSを計算
    int tx_sps = get_samples_per_symbol(TX_SAMP_RATE);
    int rx_sps = get_samples_per_symbol(RX_SAMP_RATE);
    int output_sps = get_samples_per_symbol(get_channel_spacing_hz());
    // 期待されるサンプル数を計算
    int tx_expected_len = TEST_BITS * tx_sps;
    int rx_expected_len = TEST_BITS * rx_sps;
    int output_expected_len = TEST_BITS * output_sps;
    // 各信号格納用バッファを確保
    double complex *tx_baseband = malloc(sizeof(double complex) * tx_expected_len);
    double complex *tx_upsampled = malloc(sizeof(double complex) * rx_expected_len);
    double complex *mixed_signal = malloc(sizeof(double complex) * rx_expected_len);
    double complex(*channelizer_out)[output_expected_len] =
        malloc(sizeof(double complex) * NUM_CHANNELS * output_expected_len);
    if (!tx_baseband || !tx_upsampled || !mixed_signal || !channelizer_out) {
        fprintf(stream, "Memory allocation failed\n");
        free(tx_baseband);
        free(tx_upsampled);
        free(mixed_signal);
        free(channelizer_out);
        return -1;
    }
    // 入力と出力データ用バッファを確保
    uint8_t tx_bits[TEST_BITS];
    uint8_t rx_bits[output_expected_len];
    // テスト用のガウスフィルタ係数の長さを計算
    int input_gauss_len = get_gaussian_filter_length(TX_SAMP_RATE);
    int output_gauss_len = get_gaussian_filter_length(get_channel_spacing_hz());
    // チャネライザ出力の電力スペクトル格納用バッファ
    double power[NUM_CHANNELS];
    // 結果表示用の変数
    double channel_center_hz;
    double second_power;
    int second_channel;
    int failures = 0;

    // チャネル番号から中心周波数を計算
    if (!get_channel_center_hz(channel, &channel_center_hz)) {
        fprintf(stream, "Invalid test channel %d\n", channel);
        free(tx_baseband);
        free(tx_upsampled);
        free(mixed_signal);
        free(channelizer_out);
        return -1;
    }

    if (tx_sps < 0 || rx_sps < 0 || output_sps < 0 || input_gauss_len < 0 || output_gauss_len < 0) {
        fprintf(stream, "Unsupported symbol/sample rate configuration\n");
        free(tx_baseband);
        free(tx_upsampled);
        free(mixed_signal);
        free(channelizer_out);
        return -1;
    }

    // ガウスフィルタ係数の構築
    double input_gauss[input_gauss_len];
    double output_gauss[output_gauss_len];
    if (build_gaussian_filter_for_rate(TX_SAMP_RATE, input_gauss, input_gauss_len) != 0 ||
        build_gaussian_filter_for_rate(get_channel_spacing_hz(), output_gauss, output_gauss_len) != 0) {
        fprintf(stream, "Failed to build Gaussian filters for modem test\n");
        free(tx_baseband);
        free(tx_upsampled);
        free(mixed_signal);
        free(channelizer_out);
        return -1;
    }

    // テスト用のビット列を生成
    generate_bits(tx_bits, TEST_BITS);

    fprintf(stream, "Channelizer modem loopback test start\n");
    fprintf(stream, "  channel = %d\n", channel);
    fprintf(stream, "  center_frequency = %+.0f Hz\n", channel_center_hz);
    fprintf(stream, "  tx_bits = %d\n", TEST_BITS);
    fprintf(stream, "  tx_sps = %d, rx_sps = %d, output_sps = %d\n", tx_sps, rx_sps, output_sps);

    for (int mode = 0; mode < 2; ++mode) {
        bool use_gaussian = (mode == 1);
        const char *name = use_gaussian ? "GFSK" : "FSK";
        int n_tx_samples = 0;
        int n_resampled_samples = 0;
        int n_rx_bits = 0;
        int best_shift = 0;
        int overlap = 0;
        int bit_errors;
        int detected_channel;
        double ber = 1.0;

        // 送信ビット列をFSK/GFSK変調してベースバンド信号を生成
        if (fsk_modulate_at_rate(tx_bits, TEST_BITS, TX_SAMP_RATE, input_gauss, input_gauss_len, tx_baseband,
                                 &n_tx_samples, use_gaussian) != 0) {
            fprintf(stream, "  [%s] modulation failed\n", name);
            failures++;
            continue;
        }

        // 送信サンプル列を受信側のサンプルレートへアップサンプリング
        n_resampled_samples = resample_complex_linear(tx_baseband, n_tx_samples, TX_SAMP_RATE, RX_SAMP_RATE,
                                                      tx_upsampled, rx_expected_len);
        if (n_resampled_samples < 0) {
            fprintf(stream, "  [%s] resampling failed\n", name);
            failures++;
            continue;
        }

        // 変調された信号を指定した搬送波（キャリア）周波数でシフトしてチャネライザ入力用の複素信号を生成
        build_shifted_modulated_block(tx_upsampled, n_resampled_samples, RX_SAMP_RATE, channel_center_hz, mixed_signal,
                                      rx_expected_len);

        channelizer_reset(handle);

        // チャネライザ処理の実行
        channelizer_process_block(NUM_CHANNELS, output_expected_len, COEF_PER_STAGE, handle->reg, handle->split_filter,
                                  &handle->fftw, mixed_signal, (double complex *)channelizer_out, power);

        detected_channel = find_strongest_channel(power, &second_channel, &second_power);

        // 最も強いチャネルの出力をFSK/GFSK復調してビット列を回復
        if (fsk_demodulate_at_rate(channelizer_out[channel], output_expected_len, get_channel_spacing_hz(),
                                   output_gauss, output_gauss_len, rx_bits, output_expected_len, use_gaussian,
                                   &n_rx_bits) != 0) {
            fprintf(stream, "  [%s] demodulation failed\n", name);
            failures++;
            continue;
        }

        bit_errors = align_bits_and_count_errors(tx_bits, TEST_BITS, rx_bits, n_rx_bits, &best_shift, &overlap);
        if (overlap > 0)
            ber = (double)bit_errors / overlap;

        bool passed = (detected_channel == channel) && (overlap >= TEST_BITS / 2) && (bit_errors == 0);

        fprintf(stream,
                "  [%s] detected_ch=%d next_ch=%d next_power=%.3e recovered_bits=%d overlap=%d shift=%d bit_errors=%d "
                "ber=%.4f %s\n",
                name, detected_channel, second_channel, second_power, n_rx_bits, overlap, best_shift, bit_errors, ber,
                passed ? "PASS" : "FAIL");

        if (!passed)
            failures++;
    }

    if (failures == 0) {
        fprintf(stream, "Channelizer modem loopback test passed\n");
        free(tx_baseband);
        free(tx_upsampled);
        free(mixed_signal);
        free(channelizer_out);
        return 0;
    }

    fprintf(stream, "Channelizer modem loopback test failed (%d case(s))\n", failures);
    free(tx_baseband);
    free(tx_upsampled);
    free(mixed_signal);
    free(channelizer_out);
    return -1;
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
