#include "pcap_writer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define PCAP_MAGIC_USEC_LE 0xA1B2C3D4u
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4
#define LINKTYPE_IEEE802_15_4_NOFCS 230u

typedef struct {
    int fd;
    pcap_writer_mode_t mode;
    bool initialized;
    char path[1024];
} pcap_writer_state_t;

static pcap_writer_state_t g_writer = {
    .fd = -1,
    .mode = PCAP_WRITER_MODE_DISABLED,
    .initialized = false,
    .path = {0},
};

static bool write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;

    while (written < len) {
        ssize_t rc = write(fd, p + written, len - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written += (size_t)rc;
    }

    return true;
}

static bool write_u16_le(int fd, uint16_t value) {
    uint8_t buf[2] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
    };
    return write_all(fd, buf, sizeof(buf));
}

static bool write_u32_le(int fd, uint32_t value) {
    uint8_t buf[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF),
    };
    return write_all(fd, buf, sizeof(buf));
}

static bool write_global_header(int fd) {
    return write_u32_le(fd, PCAP_MAGIC_USEC_LE) && write_u16_le(fd, PCAP_VERSION_MAJOR) &&
           write_u16_le(fd, PCAP_VERSION_MINOR) && write_u32_le(fd, 0) && write_u32_le(fd, 0) &&
           write_u32_le(fd, 4096) && write_u32_le(fd, LINKTYPE_IEEE802_15_4_NOFCS);
}

bool pcap_writer_init(const pcap_writer_config_t *config) {
    if (config == NULL || config->mode == PCAP_WRITER_MODE_DISABLED) {
        return true;
    }
    if (config->path == NULL || config->path[0] == '\0') {
        fprintf(stderr, "PCAP writer path is empty\n");
        return false;
    }

    int fd = -1;
    if (config->mode == PCAP_WRITER_MODE_FILE) {
        fd = open(config->path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0) {
            fprintf(stderr, "Failed to open PCAP file '%s': %s\n", config->path, strerror(errno));
            return false;
        }
    } else if (config->mode == PCAP_WRITER_MODE_FIFO) {
        struct stat st;
        if (stat(config->path, &st) != 0) {
            if (errno != ENOENT) {
                fprintf(stderr, "Failed to stat FIFO '%s': %s\n", config->path, strerror(errno));
                return false;
            }
            if (mkfifo(config->path, 0666) != 0) {
                fprintf(stderr, "Failed to create FIFO '%s': %s\n", config->path, strerror(errno));
                return false;
            }
        } else if (!S_ISFIFO(st.st_mode)) {
            fprintf(stderr, "Path '%s' exists but is not a FIFO\n", config->path);
            return false;
        }

        fd = open(config->path, O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            fprintf(stderr, "Failed to open FIFO '%s': %s\n", config->path, strerror(errno));
            fprintf(stderr, "Start Wireshark on the FIFO first, then rerun the sniffer.\n");
            return false;
        }
    } else {
        fprintf(stderr, "Unknown PCAP writer mode\n");
        return false;
    }

    if (!write_global_header(fd)) {
        fprintf(stderr, "Failed to write PCAP global header to '%s': %s\n", config->path, strerror(errno));
        close(fd);
        return false;
    }

    g_writer.fd = fd;
    g_writer.mode = config->mode;
    g_writer.initialized = true;
    snprintf(g_writer.path, sizeof(g_writer.path), "%s", config->path);

    return true;
}

void pcap_writer_close(void) {
    if (g_writer.fd >= 0) {
        close(g_writer.fd);
    }

    g_writer.fd = -1;
    g_writer.mode = PCAP_WRITER_MODE_DISABLED;
    g_writer.initialized = false;
    g_writer.path[0] = '\0';
}

bool pcap_writer_is_enabled(void) { return g_writer.initialized; }

bool pcap_writer_write_packet(const uint8_t *frame, size_t frame_len) {
    if (!g_writer.initialized) {
        return true;
    }
    if (frame == NULL || frame_len == 0) {
        fprintf(stderr, "PCAP writer received an empty frame\n");
        return false;
    }
    if (frame_len > UINT32_MAX) {
        fprintf(stderr, "Frame too large for PCAP record: %zu bytes\n", frame_len);
        return false;
    }

    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        fprintf(stderr, "gettimeofday failed: %s\n", strerror(errno));
        return false;
    }

    uint32_t captured_len = (uint32_t)frame_len;
    if (!write_u32_le(g_writer.fd, (uint32_t)tv.tv_sec) || !write_u32_le(g_writer.fd, (uint32_t)tv.tv_usec) ||
        !write_u32_le(g_writer.fd, captured_len) || !write_u32_le(g_writer.fd, captured_len) ||
        !write_all(g_writer.fd, frame, frame_len)) {
        fprintf(stderr, "Failed to write packet to '%s': %s\n", g_writer.path, strerror(errno));
        return false;
    }

    return true;
}
