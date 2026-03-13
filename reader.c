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
extern BlockingRingBuffer rb;
// ------------------------------------------------

void *reader_thread(void *arg)
{
    unsigned int i = 0;

    static iq_sample_t output_buf[OUTPUT_ELEMS];

    while (atomic_load(&running))
    {
        brb_read(&rb, output_buf);

        printf("%d\t%d\t%d\n", i, output_buf[0], output_buf[1]);

        i++;
    }

    // Stop
    atomic_store(&running, false);

    return NULL;
}