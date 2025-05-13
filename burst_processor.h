#ifndef __BURST_PROCESSOR_H__
#define __BURST_PROCESSOR_H__

#include <liquid/liquid.h>

// Queue size (Must be power of two)
#define BURST_OUTPUT_QUEUE_SIZE 1024

// Burst processor handle
typedef struct _burst_processor_handle
{
    // Channel number
    int ch;
    // AGC handle
    agc_crcf q;
} burst_processor_handle;

#endif