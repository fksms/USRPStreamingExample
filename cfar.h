#ifndef __CFAR_H__
#define __CFAR_H__

#include <stdio.h>

// CFAR定義
#define CFAR_ALPHA 10.0
#define CFAR_GUARD 2
#define CFAR_TRAIN 8

void get_sorted_channel_indices(size_t num_channels, size_t *sorted_idx);

#endif // __CFAR_H__
