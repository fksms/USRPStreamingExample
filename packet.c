#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PREAMBLE_MIN 4    // [Bytes]
#define PREAMBLE_MAX 1000 // [Bytes]

void analyze_packet(const uint8_t *rx_bits, int n_rx_bits) {
    // プリアンブルパターン（8ビット）
    const uint8_t preamble_pattern[8] = {0, 1, 0, 1, 0, 1, 0, 1}; // 0x55
    // SFDパターン
    const uint8_t fec_coded_sfd1[16] = {0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0};   // 0x6F4E
    const uint8_t fec_coded_sfd2[16] = {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1};   // 0x632D
    const uint8_t fec_uncoded_sfd1[16] = {1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0}; // 0x904E
    const uint8_t fec_uncoded_sfd2[16] = {0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0}; // 0x7A0E

    // プリアンブルの開始位置と長さ
    int preamble_start = -1;
    int preamble_len = 0;

    // プリアンブル探索（ビット単位）
    for (int i = 0; i <= n_rx_bits - 8 * PREAMBLE_MIN; ++i) {
        int cnt = 0;
        while (cnt < PREAMBLE_MAX) {
            if (i + (cnt + 1) * 8 > n_rx_bits) {
                break;
            }
            bool match = true;
            for (int j = 0; j < 8; ++j) {
                if (rx_bits[i + cnt * 8 + j] != preamble_pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                cnt++;
            } else {
                break;
            }
        }
        if (cnt >= PREAMBLE_MIN) {
            preamble_start = i;
            preamble_len = cnt;
            break;
        }
    }

    // プリアンブルが見つからない場合は終了
    if (preamble_start < 0) {
        fprintf(stdout, "Preamble not found\n");
        return;
    }

    // SFDの開始位置と長さ
    int sfd_bitpos = preamble_start + preamble_len * 8;
    int sfd_len = 16;

    // SFDがビット列の範囲を越えた場合は終了
    if (sfd_bitpos + sfd_len > n_rx_bits) {
        fprintf(stdout, "SFD out of range\n");
        return;
    }

    // SFD比較
    bool sfd_match = false;
    if (memcmp(&rx_bits[sfd_bitpos], fec_coded_sfd1, sfd_len) == 0) {
        sfd_match = true;
        fprintf(stdout, "SFD matched: FEC coded SFD1\n");
    } else if (memcmp(&rx_bits[sfd_bitpos], fec_coded_sfd2, sfd_len) == 0) {
        sfd_match = true;
        fprintf(stdout, "SFD matched: FEC coded SFD2\n");
    } else if (memcmp(&rx_bits[sfd_bitpos], fec_uncoded_sfd1, sfd_len) == 0) {
        sfd_match = true;
        fprintf(stdout, "SFD matched: FEC uncoded SFD1\n");
    } else if (memcmp(&rx_bits[sfd_bitpos], fec_uncoded_sfd2, sfd_len) == 0) {
        sfd_match = true;
        fprintf(stdout, "SFD matched: FEC uncoded SFD2\n");
    }

    // SFDが見つからない場合は終了
    if (!sfd_match) {
        fprintf(stdout, "SFD not found\n");
        return;
    }

    // PHRの開始位置と長さ
    int phr_bitpos = sfd_bitpos + sfd_len;
    int phr_len = 16;

    // PHRがビット列の範囲を越えた場合は終了
    if (phr_bitpos + phr_len > n_rx_bits) {
        fprintf(stdout, "PHR out of range\n");
        return;
    }

    // PHR取得
    uint16_t phr = 0;
    for (int i = 0; i < phr_len; ++i) {
        phr |= (uint16_t)(rx_bits[phr_bitpos + i] & 1) << (phr_len - 1 - i);
    }

    // ペイロード長とデータホワイトニング、FCSタイプをPHRから抽出
    int frame_len = (phr >> 0) & 0x7FF;     // bit0〜10
    int data_whitening = (phr >> 11) & 0x1; // bit11
    int fcs_type = (phr >> 12) & 0x1;       // bit12

    fprintf(stdout, "PHR: frame_len=%d, data_whitening=%d, fcs_type=%d\n", frame_len, data_whitening, fcs_type);

    // ペイロードの開始位置と長さ
    int payload_bitpos = phr_bitpos + phr_len;
    int payload_bits = frame_len * 8;

    // ペイロードがビット列の範囲を越えた場合は終了
    if (payload_bitpos + payload_bits > n_rx_bits) {
        fprintf(stdout, "Payload out of range\n");
        return;
    }

    // ペイロードを出力
    int n_bytes = (payload_bits + 7) / 8;
    fprintf(stdout, "Payload bytes: ");
    for (int i = 0; i < n_bytes; ++i) {
        uint8_t val = 0;
        for (int b = 0; b < 8; ++b) {
            int bit_idx = payload_bitpos + i * 8 + b;
            if (bit_idx < payload_bitpos + payload_bits) {
                val = (val << 1) | (rx_bits[bit_idx] & 1);
            } else {
                val = (val << 1);
            }
        }
        fprintf(stdout, "%02X ", val);
    }
    fprintf(stdout, "\n");
}
