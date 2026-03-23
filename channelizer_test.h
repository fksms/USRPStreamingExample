#ifndef __CHANNELIZER_TEST_H__
#define __CHANNELIZER_TEST_H__

#include <stdint.h>
#include <stdio.h>

#include "channelizer.h"

#define TEST_BITS_LEN 1600                    // モデムループバックテストで使用するビット数
#define PREAMBLE_LEN 8                        // プリアンブルビット数
#define TEST_RECEIVE_LEN (TEST_BITS_LEN + 10) // 復調後のビット列を格納するバッファ長（TEST_BITS_LEN + 余裕）

void generate_bits(uint8_t *bits, int len);
int channelizer_run_single_tone_test(channelizer_handle *handle, FILE *stream);
int channelizer_run_modem_loopback_test(channelizer_handle *handle, int channel, FILE *stream);
void write_iq_file(const char *path, const float *iq, int n);

#endif // __CHANNELIZER_TEST_H__