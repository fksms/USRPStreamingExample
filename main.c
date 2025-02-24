#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>

#include <uhd.h>

#define C_FEK_ARRAY_BLOCKING_QUEUE_INTEGER_IMPLEMENTATION
#define C_FEK_FAIR_LOCK_IMPLEMENTATION

#include "array_blocking_queue_integer.h"
#include "usrp.h"
#include "fft.h"
#include "display.h"

// ---------------------Status---------------------
volatile sig_atomic_t running = 1;
pthread_mutex_t mutex;
// ------------------------------------------------

// ---------------For USRP Streaming---------------
// Center frequency
double freq = 2400e6;
// Sampling rate
double rate = 10e6;
// Gain
double gain = 30.0;
// Device args
char *device_args = NULL;
// Channel
size_t channel = 0;

// ストリーミングデータを格納するためのバッファ
stream_data_t *stream_buffer;
// バッファ内のデータが格納された位置を格納
Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

// --------------------For FFT---------------------
// FFT Size (Must be power of two)
int fft_size = 1024;

// FFTデータを格納
double *fft_data;
// バッファ内のデータが格納された位置を格納
Array_Blocking_Queue_Integer abq2;
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
            break;

        default:
            print_help();
            break;
        }
    }

    if (!device_args)
        device_args = strdup("");

    // Init the blocking queue.
    if (blocking_queue_init(&abq1, RX_STREAMER_RECV_QUEUE_SIZE))
    {
        printf("Init blocking queue failed\n");
        return 1;
    }

    if (blocking_queue_init(&abq2, FFT_DATA_QUEUE_SIZE))
    {
        printf("Init blocking queue failed\n");
        return 1;
    }

    // Init mutex
    if (pthread_mutex_init(&mutex, NULL))
    {
        printf("Init mutex failed\n");
        return 1;
    }

    // USRP handle
    uhd_usrp_handle usrp;

    // Setup USRP
    if (usrp_setup(&usrp))
    {
        printf("Setup USRP failed\n");
        return 1;
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
        return 1;
    }

    // -----------------Setting pthread attributes-----------------
    pthread_attr_t attr;
    struct sched_param param;

    // Initialize pthread attributes
    if (pthread_attr_init(&attr))
    {
        printf("Init pthread attributes failed\n");
        return 1;
    }

    // Set scheduler policy and priority of pthread
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO))
    {
        printf("Pthread setschedpolicy failed\n");
        return 1;
    }

    param.sched_priority = 99;

    if (pthread_attr_setschedparam(&attr, &param))
    {
        printf("Pthread setschedparam failed\n");
        return 1;
    }

    // Use scheduling parameters of attr
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))
    {
        printf("Pthread setinheritsched failed\n");
        return 1;
    }
    // ------------------------------------------------------------

    pthread_t usrpStreamThread;
    pthread_t fftThread;
    pthread_t displayThread;

    // Create Display thread
    if (pthread_create(&displayThread, NULL, display_thread, NULL))
    {
        printf("Create display_thread failed\n");
        return 1;
    }

    // Create FFT thread
    if (pthread_create(&fftThread, NULL, fft_thread, NULL))
    {
        printf("Create fft_thread failed\n");
        return 1;
    }

    // Create USRP stream thread
    if (pthread_create(&usrpStreamThread, &attr, usrp_stream_thread, (void *)&usrp_rx))
    {
        printf("Create usrp_stream_thread failed\n");
        return 1;
    }

    // Join USRP stream thread
    if (pthread_join(fftThread, NULL))
    {
        printf("Join fft_thread failed\n");
        return 1;
    }

    // Join FFT thread
    if (pthread_join(usrpStreamThread, NULL))
    {
        printf("Join usrp_stream_thread failed\n");
        return 1;
    }

    // Join Display thread
    if (pthread_join(displayThread, NULL))
    {
        printf("Join display_thread failed\n");
        return 1;
    }

    // Closes the blocking queue.
    blocking_queue_destroy(&abq1);
    blocking_queue_destroy(&abq2);

    // Destroy mutex
    if (pthread_mutex_destroy(&mutex))
    {
        printf("Destroy mutex failed\n");
        return 1;
    }

    // Close USRP RX
    if (usrp_rx_close(&usrp_rx))
    {
        printf("Close USRP RX failed\n");
        return 1;
    }

    // Close USRP
    if (usrp_close(usrp))
    {
        printf("Close USRP failed\n");
        return 1;
    }

    return 0;
}