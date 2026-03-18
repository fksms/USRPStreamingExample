#ifndef __BURST_CATCHER_H__
#define __BURST_CATCHER_H__

#include <complex.h>
#include <stdbool.h>

#include "channelizer.h"

#define MAX_FRAMES 100

typedef struct {
    double complex buf[MAX_FRAMES * TIME_SLOTS]; // バーストを格納
    int len;                                     // 蓄積済みサンプル数
    bool active;                                 // バースト検出中かどうか
} BurstCatcher;

#endif // __BURST_CATCHER_H__