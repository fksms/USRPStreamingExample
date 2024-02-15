#ifndef __STRUCTURE_H__
#define __STRUCTURE_H__

#include <stdint.h>

typedef struct _sample_buf_t {
    size_t num_of_samples;
    int16_t samples[];
} sample_buf_t;

#endif
