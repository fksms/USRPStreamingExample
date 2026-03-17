#include <getopt.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <uhd.h>

#include "channelizer.h"
#include "lfrb.h"
#include "usrp.h"

// ---------------------Status---------------------
_Atomic bool running = true;
// ------------------------------------------------

// --------------------For USRP--------------------
// Device args (e.g. "type=b200")
char *device_args = "";
// ------------------------------------------------

// ------------------For USRP RX-------------------
// Center frequency
double rx_freq = 924e6;
// Sampling rate
double rx_rate = 10e6;
// Gain
double rx_gain = 30.0;
// Channel (0 or 1)
size_t rx_channel = 0;
// Antenna ("TX/RX" or "RX2")
char *rx_antenna = "RX2";

// ストリーミングデータを格納するためのバッファ
LockFreeRingBuffer rb;
// ------------------------------------------------

void print_help(void) {
    fprintf(stderr, "XXXXXX\n\n"

                    "Options:\n"
                    "    -d (device args)\n"
                    "    -a (RX antenna)\n"
                    "    -c (RX channel)\n"
                    "    -f (RX frequency in Hz)\n"
                    "    -r (RX sample rate in Hz)\n"
                    "    -g (RX gain)\n"
                    "    -h (print this help message)\n");
}

int main(int argc, char *argv[]) {
    int option = 0;
    // Process options
    while ((option = getopt(argc, argv, "d:a:c:f:r:g:h")) != -1) {
        switch (option) {
        case 'd':
            device_args = strdup(optarg);
            break;

        case 'a':
            rx_antenna = strdup(optarg);
            break;

        case 'c':
            rx_channel = atoi(optarg);
            break;

        case 'f':
            rx_freq = atof(optarg);
            break;

        case 'r':
            rx_rate = atof(optarg);
            break;

        case 'g':
            rx_gain = atof(optarg);
            break;

        case 'h':
            print_help();
            return 0;

        default:
            print_help();
            return 0;
        }
    }

    // Init the ring buffer
    lfrb_init(&rb);

    // ----------------------------Setup---------------------------
    // USRP handle
    uhd_usrp_handle usrp;

    // Setup USRP
    if (usrp_setup(&usrp)) {
        printf("Setup USRP failed\n");
        return -1;
    }

    // USRP RX handle
    uhd_usrp_rx_handle usrp_rx = {
        .usrp = usrp,
        .rx_streamer = NULL,
        .rx_metadata = NULL,
    };

    // Setup USRP RX
    if (usrp_rx_setup(&usrp_rx)) {
        printf("Setup USRP RX failed\n");
        return -1;
    }

    // USRP TX handle
    uhd_usrp_tx_handle usrp_tx = {
        .usrp = usrp,
        .tx_streamer = NULL,
        .tx_metadata = NULL,
    };

    // Setup USRP TX
    if (usrp_tx_setup(&usrp_tx)) {
        printf("Setup USRP TX failed\n");
        return -1;
    }

    // Setup channelizer
    channelizer_handle channelizer;
    if (channelizer_setup(&channelizer)) {
        printf("Setup channelizer failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // -----------------Setting pthread attributes-----------------
    pthread_attr_t attr;
    struct sched_param param;

    // Initialize pthread attributes
    if (pthread_attr_init(&attr)) {
        printf("Init pthread attributes failed\n");
        return -1;
    }

    // Set scheduler policy and priority of pthread
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) {
        printf("Pthread setschedpolicy failed\n");
        return -1;
    }

    param.sched_priority = 99;

    if (pthread_attr_setschedparam(&attr, &param)) {
        printf("Pthread setschedparam failed\n");
        return -1;
    }

    // Use scheduling parameters of attr
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) {
        printf("Pthread setinheritsched failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // ------------------------Create thread-----------------------
    pthread_t usrpRxThread;
    pthread_t usrpTxThread;
    pthread_t channelizerThread;

    // Create channelizer thread
    if (pthread_create(&channelizerThread, &attr, channelizer_thread, (void *)&channelizer)) {
        printf("Create channelizer thread failed\n");
        return -1;
    }

    // Create USRP RX thread
    if (pthread_create(&usrpRxThread, &attr, usrp_rx_thread, (void *)&usrp_rx)) {
        printf("Create USRP RX thread failed\n");
        return -1;
    }

    // Create USRP TX thread
    if (pthread_create(&usrpTxThread, &attr, usrp_tx_thread, (void *)&usrp_tx)) {
        printf("Create USRP TX thread failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // -------------------------Join thread------------------------
    // Join USRP TX thread
    if (pthread_join(usrpTxThread, NULL)) {
        printf("Join USRP TX thread failed\n");
        return -1;
    }

    // Join USRP RX thread
    if (pthread_join(usrpRxThread, NULL)) {
        printf("Join USRP RX thread failed\n");
        return -1;
    }

    // Join channelizer thread
    if (pthread_join(channelizerThread, NULL)) {
        printf("Join channelizer thread failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // ----------------------------Close---------------------------
    // Close channelizer
    if (channelizer_close(&channelizer)) {
        printf("Close channelizer failed\n");
        return -1;
    }

    // Close USRP TX
    if (usrp_tx_close(&usrp_tx)) {
        printf("Close USRP TX failed\n");
        return -1;
    }

    // Close USRP RX
    if (usrp_rx_close(&usrp_rx)) {
        printf("Close USRP RX failed\n");
        return -1;
    }

    // Close USRP
    if (usrp_close(usrp)) {
        printf("Close USRP failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    return 0;
}