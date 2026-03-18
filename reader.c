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

    while (atomic_load(&running)) {
        // 配列取得用のポインタと長さを用意
        double complex *data = NULL;
        int length = 0;

        // バッファから配列を受け取る（ブロッキング）
        if (brb_read(&brb, &data, &length)) {

            printf("Reader: Received burst of length %d samples\n", length);

            // 受け取った配列をfree
            free(data);
        }
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}