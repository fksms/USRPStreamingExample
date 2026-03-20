#include <complex.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "brb.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// ---------------------Buffer---------------------
extern BlockingRingBuffer brb;
// ------------------------------------------------

// リングバッファから配列を受け取り、freeするだけのスレッド
void *reader_thread(void *arg) {

    int burst_count = 0;
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
            free(data);
            burst_count++;
        }
    }

    atomic_store(&running, false);
    return NULL;
}