#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>

#include <uhd.h>

#define C_FEK_BLOCKING_QUEUE_IMPLEMENTATION
#define C_FEK_FAIR_LOCK_IMPLEMENTATION

#include "structure.h"
#include "blocking_queue.h"
#include "usrp.h"


// Queue size
#define SAMPLES_QUEUE_SIZE 32


double freq          = 500e6;
double rate          = 20e6;
double gain          = 5.0;
char* device_args    = NULL;
size_t channel       = 0;

int running = 1;

Blocking_Queue samples_queue;


void *test_thread(void *arg) {

    sample_buf_t *sb = NULL;

    int i = 0;

    while (running) {

        if (blocking_queue_take(&samples_queue, &sb)) {
            printf("Unknown error.\n");
            free(sb);
            break;
        }

        printf("%d\n", i);

        free(sb);

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
    blocking_queue_init(&samples_queue, SAMPLES_QUEUE_SIZE);

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

    // キュー内の要素（sample_buf_t）を全て取り出して解放
    sample_buf_t *sb = NULL;
    while(blocking_queue_poll(&samples_queue, &sb) == 0) {
        free(sb);
    }

    // Closes the blocking queue.
    blocking_queue_close(&samples_queue);

    return 0;
}