#ifndef __FFT_H__
#define __FFT_H__

#include <fftw3.h>

// Queue size (Must be power of two)
#define FFT_DATA_QUEUE_SIZE 128

// FFTW handle
typedef struct _fftw_handle
{
    // FFTW input
    fftw_complex *in;
    // FFTW output
    fftw_complex *out;
    // FFTW plan
    fftw_plan plan;
} fftw_handle;

int fft_setup(fftw_handle *fh);
void *fft_thread(void *arg);
int fft_close(fftw_handle fh);

#endif