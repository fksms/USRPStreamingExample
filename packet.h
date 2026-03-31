#ifndef __PACKET_H__
#define __PACKET_H__

#include <stdint.h>

void analyze_packet(const uint8_t *rx_bits, int n_rx_bits);

#endif // __PACKET_H__
