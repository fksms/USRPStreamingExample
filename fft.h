#ifndef __FFT_H__
#define __FFT_H__

// Queue size (Must be power of two)
#define FFT_DATA_QUEUE_SIZE 128

void *fft_thread(void *arg);

#endif