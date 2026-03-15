#include "channelizer.h"

void get_sorted_channel_indices(size_t num_channels, size_t *sorted_idx)
{
    size_t N = num_channels / 2;
    size_t idx = 0;

    if (num_channels % 2 == 0)
    {
        // 偶数チャネル
        for (size_t i = N + 1; i < num_channels; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = 0;
        for (size_t i = 1; i < N; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = N - 1;
        // 折り返し点Nは除外
    }
    else
    {
        // 奇数チャネル
        for (size_t i = N + 1; i < num_channels; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = 0;
        for (size_t i = 1; i < N; ++i)
            sorted_idx[idx++] = i;
        sorted_idx[idx++] = N;
    }
}
