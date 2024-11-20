#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

//#include <fftw3.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"


extern int running;

// ---------------For USRP Streaming---------------
extern sample_buf_t *buffs;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

// --------------------For FFT---------------------
extern int fft_size;
// ------------------------------------------------


void *test_thread(void *arg) {

    int array_index;

    unsigned int i = 0;
    
	//fftw_complex *in, *out;
	//fftw_plan plan;

	//in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size);
	//out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size);
	//plan = fftw_plan_dft_1d(fft_size, in, out, FFTW_FORWARD, FFTW_ESTIMATE);


    while (running) {

        if (blocking_queue_take(&abq1, &array_index)) {
            printf("Take queue error.\n");
            break;
        }

        printf("%d\t\t%d\t\t%d\t\t%d\n", i, array_index, buffs[array_index].samples[1020], buffs[array_index].samples[1021]);

        i++;
    }
    running = 0;

    return NULL;
}