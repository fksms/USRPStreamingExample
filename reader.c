#include <complex.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "brb.h"
#include "channelizer.h"
#include "fsk.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// ---------------------Buffer---------------------
extern BlockingRingBuffer brb;
// ------------------------------------------------

// リングバッファから配列を受け取り、freeするだけのスレッド
void *reader_thread(void *arg) {

    int burst_count = 0;

    // チャネライザから出力された信号のSPSを計算
    int sps = get_samples_per_symbol(get_channel_spacing_hz());

    // ガウスフィルタ係数の長さを計算
    int gauss_len = get_gaussian_filter_length(get_channel_spacing_hz());

    // ガウスフィルタ係数の構築
    double gauss_coef[gauss_len];
    if (build_gaussian_filter_for_rate(get_channel_spacing_hz(), gauss_coef, gauss_len) != 0) {
        printf("Failed to build Gaussian filters\n");
        // ガウスフィルタの構築に失敗した場合は強制終了
        atomic_store(&running, false);
        exit(EXIT_FAILURE);
    }

    while (atomic_load(&running) && burst_count < 20) {
        double complex *data = NULL;
        int length = 0;

        if (brb_read(&brb, &data, &length)) {
            printf("Reader: Received burst of length %d samples\n", length);

            // ファイル名生成
            char filename[32];
            snprintf(filename, sizeof(filename), "output_%d.csv", burst_count + 1);

            FILE *fp = fopen(filename, "w");
            if (!fp) {
                perror("Failed to open output_X.csv");
                free(data);
                atomic_store(&running, false);
                return NULL;
            }

            // CSV出力: 実部,虚部
            for (int i = 0; i < length; ++i) {
                fprintf(fp, "%lf,%lf\n", creal(data[i]), cimag(data[i]));
            }

            fclose(fp);

            // 受け取ったデータの長さから復調可能なビット数を計算
            int rx_bits_capacity = length / sps;

            // 復調ビット列格納用バッファ
            uint8_t rx_bits[rx_bits_capacity];

            // 復調されたビット数
            int n_rx_bits = 0;

            // チャネライザの出力をFSK/GFSK復調してビット列を回復
            if (fsk_demodulate_at_rate(data, length, get_channel_spacing_hz(), true, gauss_coef, gauss_len,
                                       rx_bits_capacity, rx_bits, &n_rx_bits) != 0) {
                printf("demodulation failed\n");
                continue;
            }

            // 復調したビット列を表示
            printf("Demodulated bits (%d bits): ", n_rx_bits);
            for (int i = 0; i < n_rx_bits; ++i) {
                printf("%d", rx_bits[i]);
            }
            printf("\n");

            free(data);
            burst_count++;
        }
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}
