#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <complex.h>

#include <uhd.h>

#include "brb.h"
#include "lfrb.h"
#include "usrp.h"
#include "reader.h"

// ---------------------Status---------------------
volatile sig_atomic_t running = 1;
pthread_mutex_t mutex;
// ------------------------------------------------

// ---------------For USRP Streaming---------------
// Center frequency
double freq = 2426e6;
// Sampling rate
double rate = 10e6;
// Gain
double gain = 40.0;
// Device args (e.g. "type=b200")
char *device_args = "";
// Channel (0 or 1)
size_t channel = 0;
// Antenna ("TX/RX" or "RX2")
char *antenna = "TX/RX";
// Number of samples per once
size_t num_samps_per_once = NUM_SAMPS_PER_RECV;

// ストリーミングデータを格納するためのバッファ
BlockingRingBuffer rb;
// ------------------------------------------------

void print_help(void)
{
    fprintf(stderr,
            "XXXXXX\n\n"

            "Options:\n"
            "    -a (device args)\n"
            "    -c (channel)\n"
            "    -f (frequency in Hz)\n"
            "    -r (sample rate in Hz)\n"
            "    -g (gain)\n"
            "    -h (print this help message)\n");
}

int main(int argc, char *argv[])
{
    int option = 0;
    // Process options
    while ((option = getopt(argc, argv, "a:c:f:r:g:h")) != -1)
    {
        switch (option)
        {
        case 'a':
            device_args = strdup(optarg);
            break;

        case 'c':
            channel = atoi(optarg);
            break;

        case 'f':
            freq = atof(optarg);
            break;

        case 'r':
            rate = atof(optarg);
            break;

        case 'g':
            gain = atof(optarg);
            break;

        case 'h':
            print_help();
            return 0;

        default:
            print_help();
            return 0;
        }
    }

    // ------------------------Create queue------------------------
    // Init the ring buffer
    brb_init(&rb);
    // ------------------------------------------------------------

    // Init mutex
    if (pthread_mutex_init(&mutex, NULL))
    {
        printf("Init mutex failed\n");
        return -1;
    }

    // ----------------------------Setup---------------------------
    // USRP handle
    uhd_usrp_handle usrp;

    // Setup USRP
    if (usrp_setup(&usrp))
    {
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
    if (usrp_rx_setup(&usrp_rx))
    {
        printf("Setup USRP RX failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // -----------------Setting pthread attributes-----------------
    pthread_attr_t attr;
    struct sched_param param;

    // Initialize pthread attributes
    if (pthread_attr_init(&attr))
    {
        printf("Init pthread attributes failed\n");
        return -1;
    }

    // Set scheduler policy and priority of pthread
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO))
    {
        printf("Pthread setschedpolicy failed\n");
        return -1;
    }

    param.sched_priority = 99;

    if (pthread_attr_setschedparam(&attr, &param))
    {
        printf("Pthread setschedparam failed\n");
        return -1;
    }

    // Use scheduling parameters of attr
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))
    {
        printf("Pthread setinheritsched failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // ------------------------Create thread-----------------------
    pthread_t usrpStreamThread;
    pthread_t readerThread;

    // Create Reader thread
    if (pthread_create(&readerThread, &attr, reader_thread, NULL))
    {
        printf("Create reader_thread failed\n");
        return -1;
    }

    // Create USRP stream thread
    if (pthread_create(&usrpStreamThread, &attr, usrp_stream_thread, (void *)&usrp_rx))
    {
        printf("Create usrp_stream_thread failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // -------------------------Join thread------------------------
    // Join USRP stream thread
    if (pthread_join(usrpStreamThread, NULL))
    {
        printf("Join usrp_stream_thread failed\n");
        return -1;
    }

    // Join Reader thread
    if (pthread_join(readerThread, NULL))
    {
        printf("Join reader_thread failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // ----------------------------Close---------------------------
    // Close USRP RX
    if (usrp_rx_close(&usrp_rx))
    {
        printf("Close USRP RX failed\n");
        return -1;
    }

    // Close USRP
    if (usrp_close(usrp))
    {
        printf("Close USRP failed\n");
        return -1;
    }
    // ------------------------------------------------------------

    // Destroy mutex
    if (pthread_mutex_destroy(&mutex))
    {
        printf("Destroy mutex failed\n");
        return -1;
    }

    // ------------------------Close queue-------------------------
    // Destroy the ring buffer
    brb_destroy(&rb);
    // ------------------------------------------------------------

    return 0;
}