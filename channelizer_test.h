#ifndef __CHANNELIZER_TEST_H__
#define __CHANNELIZER_TEST_H__

#include <stdint.h>
#include <stdio.h>

#include "channelizer.h"

#define TEST_BITS 150      // モデムループバックテストで使用するビット数
#define PREAMBLE_LEN 8     // プリアンブルビット数
#define BUFFER_SIZE 100000 // 送信信号と受信信号のバッファサイズ（余裕を持たせるため大きめに設定）

void generate_bits(uint8_t *bits, int len);
int channelizer_run_self_test(channelizer_handle *handle, FILE *stream);
int channelizer_run_modem_loopback_test(channelizer_handle *handle, int channel, FILE *stream);
void write_iq_file(const char *path, const float *iq, int n);

#endif // __CHANNELIZER_TEST_H__