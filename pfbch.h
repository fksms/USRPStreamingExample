#ifndef __PFBCH_H__
#define __PFBCH_H__

#include <liquid/liquid.h>

// Queue size (Must be power of two)
#define CHANNELIZER_OUTPUT_QUEUE_SIZE 128

int channelizer_setup(firpfbch_crcf *q);
void *channelizer_thread(void *arg);
int channelizer_close(firpfbch_crcf q);

#endif