#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include <fftw3.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern sample_buf_t *buffs;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

// --------------------For FFT---------------------
extern int fft_size;
// ------------------------------------------------

void *fft_thread(void *arg)
{
    int array_index;

    unsigned int i = 0;

    fftw_complex *in, *out;
    fftw_plan plan;

    in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fft_size);
    out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fft_size);
    plan = fftw_plan_dft_1d(fft_size, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq1, &array_index))
        {
            printf("Take queue error.\n");
            break;
        }

        // 入力データをIQから複素数形式に変換
        // 入力データは16ビットのため、2^16(=65536)で割っておく
        for (int i = 0; i < fft_size; i++)
        {
            in[i][0] = (double)buffs[array_index].samples[2 * i + 1] / 65536; // I成分
            in[i][1] = (double)buffs[array_index].samples[2 * i] / 65536;     // Q成分
        }

        // FFTの実行
        fftw_execute(plan);

        printf("%d\t\t%lf\t\t%lf\n", i, out[0][0], out[0][1]);

        // printf("%d\t\t%d\t\t%d\t\t%d\n", i, array_index, buffs[array_index].samples[1020], buffs[array_index].samples[1021]);

        i++;
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}