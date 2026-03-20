#ifndef __WRITER_H__
#define __WRITER_H__

#include <stdbool.h>
#include <stdlib.h>

#define SYMBOL_RATE 50e3 // シンボルレート [bps]
#define MOD_INDEX 0.5    // 変調指数 (h) (0.5〜1.0)
#define BT 0.5           // Gaussian BT積
#define GAUSS_SPAN 4     // ガウスフィルタスパン [symbols]

void generate_bits(uint8_t *bits, int len);
void build_gaussian_filter(double *gauss_coef, int gauss_len);
int fsk_modulate(const uint8_t *bits, int n_bits, double *gauss_coef, int gauss_len, float *iq_out, int *n_samples, bool use_gaussian);
void write_iq_file(const char *path, const float *iq, int n);

#endif // __WRITER_H__