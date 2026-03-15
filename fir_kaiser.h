#ifndef __FIR_KAISER_H__
#define __FIR_KAISER_H__

void fir_design_kaiser_lowpass(double *h, int N, double fc, double beta);

#endif // __FIR_KAISER_H__