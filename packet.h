#ifndef __PACKET_H__
#define __PACKET_H__

#include <stdbool.h>
#include <stdint.h>

bool packet_export_psdu_to_pcap(const uint8_t *psdu, int psdu_len, int fcs_type);
void analyze_packet(const uint8_t *rx_bits, int n_rx_bits);

#endif // __PACKET_H__
