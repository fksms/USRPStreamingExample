#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "brb.h"
#include "lfrb.h"

// ---------------------Status---------------------
extern _Atomic bool running;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern double rate;

extern LockFreeRingBuffer rb;
// ------------------------------------------------

void *reader_thread(void *arg)
{
    unsigned int i = 0;

    static iq_sample_t output_buf[OUTPUT_ELEMS];

    // `rate`と`OUTPUT_SAMPS`から待機時間を計算
    double wait_sec = (double)OUTPUT_SAMPS / rate;
    time_t sec = (time_t)wait_sec;
    long nsec = (long)((wait_sec - sec) * 1e9);
    struct timespec ts = {sec, nsec};

    while (atomic_load(&running))
    {
        if (!lfrb_read(&rb, output_buf))
        {
            // バッファ空の場合
            nanosleep(&ts, NULL);
            continue;
        }

        printf("%d\t%d\t%d\n", i, output_buf[0], output_buf[1]);

        i++;
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}