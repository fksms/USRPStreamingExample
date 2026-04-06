#ifndef __PCAP_WRITER_H__
#define __PCAP_WRITER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PCAP_WRITER_MODE_DISABLED = 0,
    PCAP_WRITER_MODE_FILE,
    PCAP_WRITER_MODE_FIFO,
} pcap_writer_mode_t;

typedef struct {
    pcap_writer_mode_t mode;
    const char *path;
} pcap_writer_config_t;

bool pcap_writer_init(const pcap_writer_config_t *config);
void pcap_writer_close(void);
bool pcap_writer_is_enabled(void);
bool pcap_writer_write_packet(const uint8_t *frame, size_t frame_len);

#endif // __PCAP_WRITER_H__
