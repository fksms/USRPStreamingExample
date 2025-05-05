#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <complex.h>

#include "array_blocking_queue_integer.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern size_t num_samps_per_once;
// ------------------------------------------------

// --------------------From FFT--------------------
/*
extern int fft_size;

extern double *fft_data;
extern Array_Blocking_Queue_Integer abq2;
*/
// ------------------------------------------------

// -----------From Polyphase Channelizer-----------
extern unsigned int num_channels;

/*
extern float complex *channelizer_output;
extern Array_Blocking_Queue_Integer abq2;
*/
// ------------------------------------------------

// --------------From Burst Generator--------------
extern float complex *burst_output;
extern Array_Blocking_Queue_Integer *abq3;
// ------------------------------------------------

void *take_queue_thread(void *arg)
{
    int ch = *(int *)arg;

    unsigned int i = 0;

    // チャネライズ後の1チャネルあたりのサンプル数
    unsigned int num_frames = num_samps_per_once / num_channels;

    // Array_Blocking_Queue（abq3）の何番目に格納したかを示すインデックス
    int abq3_index = 0;

    float complex sample = 0.0f;

    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq3[ch], &abq3_index))
        {
            printf("Take burst data error.\n");
            break;
        }

        sample = burst_output[abq3_index * num_samps_per_once + ch * num_frames + 0];

        printf("%d\t%d\t%d\t%f\t%f\n", ch, i, abq3_index, crealf(sample), cimagf(sample));

        i++;
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}