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

// ---------------------Status---------------------
volatile sig_atomic_t running = 1;
pthread_mutex_t mutex;
// ------------------------------------------------

// ---------------For USRP Streaming---------------
// Center frequency
double freq = 2400e6;
// Sampling rate
double rate = 12.5e6;
// Gain
double gain = 30.0;
// Device args
char *device_args = NULL;
// Channel
size_t channel = 0;

// サンプルを格納するためのバッファ
stream_buf_t *stream_buffer;
// バッファ内のサンプルが格納された位置を格納
Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

// --------------------For FFT---------------------
// FFT Size (Must be power of two)
int fft_size = 1024;
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
    blocking_queue_init(&abq1, RX_STREAMER_RECV_QUEUE_SIZE);

    // USRP handle
    uhd_usrp_handle usrp = NULL;

    // USRP setup
    usrp = usrp_setup();

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

    // Create FFT thread
    if (pthread_create(&fftThread, NULL, fft_thread, NULL))
    {
        printf("Create test_thread failed\n");
        return 1;
    }

    // Create USRP stream thread
    if (pthread_create(&usrpStreamThread, &attr, usrp_stream_thread, (void *)usrp))
    {
        printf("Create usrp_stream_thread failed\n");
        return 1;
    }

    // Join USRP stream thread
    if (pthread_join(fftThread, NULL))
    {
        printf("Join test_thread failed\n");
        return 1;
    }

    // Join FFT thread
    if (pthread_join(usrpStreamThread, NULL))
    {
        printf("Join usrp_stream_thread failed\n");
        return 1;
    }

    // メモリ解放
    free(stream_buffer);

    // Closes the blocking queue.
    blocking_queue_destroy(&abq1);

    // Close USRP
    usrp_close(usrp);

    return 0;
}