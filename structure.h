#ifndef __STRUCTURE_H__
#define __STRUCTURE_H__

#include <stdint.h>

typedef struct _sample_buf_t {
    unsigned long num;
    int8_t samples[];
} sample_buf_t;

#endif
