#ifndef __CFAR_H__
#define __CFAR_H__

/* ---------------------------------------------------------------
 * CFAR_ALPHA: CFAR閾値のスケーリングファクタ
 * CFAR_GUARD: 片側のガードセルの数
 * CFAR_TRAIN: 片側のトレーニングセルの数
 * ---------------------------------------------------------------*/
#define CFAR_ALPHA 100.0 // 100倍 = 20dBに相当するスケーリングファクタ
#define CFAR_GUARD 2
#define CFAR_TRAIN 8

#endif // __CFAR_H__
