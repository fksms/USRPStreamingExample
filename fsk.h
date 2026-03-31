#ifndef __FSK_H__
#define __FSK_H__

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define SYMBOL_RATE 100e3 // シンボルレート [bps]
#define MOD_INDEX 0.5     // 変調指数 (h) (0.5〜1.0)
#define BT 0.5            // Gaussian BT積
#define GAUSS_SPAN 4      // ガウスフィルタスパン [symbols]

int get_samples_per_symbol(double sample_rate_hz);
int get_gaussian_filter_length(double sample_rate_hz);
int build_gaussian_filter_for_rate(double sample_rate_hz, double *gauss_coef, int gauss_len);
int fsk_modulate_at_rate(const uint8_t *bits, int n_bits, double sample_rate_hz, bool use_gaussian,
                         const double *gauss_coef, int gauss_len, double complex *iq_out, int *n_samples);
int fsk_demodulate_at_rate(const double complex *iq_in, int n_samples, double sample_rate_hz, bool use_gaussian,
                           const double *gauss_coef, int gauss_len, int max_bits, uint8_t *bits_out, int *n_bits_out);

#endif // __FSK_H__
