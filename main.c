#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>

#include <uhd.h>

#define C_FEK_ARRAY_BLOCKING_QUEUE_INTEGER_IMPLEMENTATION
#define C_FEK_FAIR_LOCK_IMPLEMENTATION

#include "structure.h"
#include "array_blocking_queue_integer.h"
#include "usrp.h"


// --------------------global--------------------
// Center frequency
double freq          = 500e6;
// Sampling rate
double rate          = 20e6;
// Gain
double gain          = 5.0;
// Device args
char* device_args    = NULL;
// Channel
size_t channel       = 0;


// サンプルを格納するためのバッファ
sample_buf_t *buffs;
// バッファ内のサンプルが格納された位置を格納
Array_Blocking_Queue_Integer bq1;


// 動作ステータス
int running = 1;
// ----------------------------------------------


void *test_thread(void *arg) {

    int array_index;

    unsigned int i = 0;

    while (running) {

        if (blocking_queue_take(&bq1, &array_index)) {
            printf("Unknown error.\n");
            break;
        }

        printf("%d\t\t%d\n", i, array_index);

        i++;
    }
    running = 0;

    return NULL;
}


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

int main(int argc, char* argv[])
{
    int option = 0;
    // Process options
    while ((option = getopt(argc, argv, "a:c:f:r:g:h")) != -1) {
        switch (option) {
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



    // USRP handle
    uhd_usrp_handle usrp = NULL;

    struct sched_param param;

    pthread_t test;
    pthread_t usrp_stream;
    pthread_attr_t attr;


    // Init the blocking queue.
    blocking_queue_init(&bq1, SAMPLES_QUEUE_SIZE);

    // USRP setup
    usrp = usrp_setup();


    // Initialize pthread attributes
    if (pthread_attr_init(&attr)) {
        printf("init pthread attributes failed\n");
        return 1;
    }

    // Set scheduler policy and priority of pthread
    if (pthread_attr_setschedpolicy(&attr, SCHED_RR)) {
        printf("pthread setschedpolicy failed\n");
        return 1;
    }

    param.sched_priority = 127;

    if (pthread_attr_setschedparam(&attr, &param)) {
        printf("pthread setschedparam failed\n");
        return 1;
    }

        // Use scheduling parameters of attr
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) {
        printf("pthread setinheritsched failed\n");
        return 1;
    }

    // Create thread
    if (pthread_create(&test, NULL, test_thread, NULL)) {
        printf("create pthread failed\n");
        return 1;
    }

    if (pthread_create(&usrp_stream, &attr, usrp_stream_thread, (void *)usrp)) {
        printf("create pthread failed\n");
        return 1;
    }


    while (running) {
        pause();
    }
    running = 0;

    // メモリ解放
    free(buffs);

    // Closes the blocking queue.
    blocking_queue_close(&bq1);

    return 0;
}