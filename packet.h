#ifndef __PACKET_H__
#define __PACKET_H__

#include <stdint.h>

void extract_frame(const uint8_t *rx_bits, int n_rx_bits);

#endif // __PACKET_H__
