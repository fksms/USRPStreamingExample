#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <complex.h>

#include "brb.h"
#include "lfrb.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern BlockingRingBuffer rb;
// ------------------------------------------------

void *reader_thread(void *arg)
{
    unsigned int i = 0;

    static iq_sample_t output_buf[OUTPUT_ELEMS];

    while (running)
    {
        brb_read(&rb, output_buf);

        printf("%d\t%d\t%d\n", i, output_buf[0], output_buf[1]);

        i++;
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}