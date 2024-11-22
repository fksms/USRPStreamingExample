#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "array_blocking_queue_integer.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------------From FFT--------------------
extern int fft_size;

extern double *fft_data;
extern Array_Blocking_Queue_Integer abq2;
// ------------------------------------------------

void *display_thread(void *arg)
{
    unsigned int i = 0;

    // Array_Blocking_Queue（abq2）の何番目に格納したかを示すインデックス
    int abq2_index = 0;

    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq2, &abq2_index))
        {
            printf("Take FFT data error.\n");
            break;
        }

        printf("%d\t\t%lf\n", i, fft_data[abq2_index * fft_size + 1]);

        i++;
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}