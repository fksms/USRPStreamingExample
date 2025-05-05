#ifndef __PFBCH_H__
#define __PFBCH_H__

#include <liquid/liquid.h>

// Queue size (Must be power of two)
#define CHANNELIZER_OUTPUT_QUEUE_SIZE 1024

int channelizer_setup(firpfbch_crcf *q);
void *channelizer_thread(void *arg);
int channelizer_close(firpfbch_crcf q);

int burst_generator_setup(void);
void *burst_generator_thread(void *arg);
int burst_generator_close(void);

#endif