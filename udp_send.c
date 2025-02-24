#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"
#include "udp_send.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern double rate;

extern stream_data_t *stream_buffer;
extern Array_Blocking_Queue_Integer abq1;
// ------------------------------------------------

int socket_setup(socket_handle *sock)
{
    // Create socket
    if ((sock->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed\n");
        return -1;
    }

    // Set addless family
    sock->server_addr.sin_family = AF_INET;
    // Set port
    sock->server_addr.sin_port = htons(SERVER_PORT);
    // Set IP address
    if (inet_pton(AF_INET, SERVER_IP, &sock->server_addr.sin_addr) <= 0)
    {
        perror("Invalid address\n");
        return -1;
    }

    return 0;
}

void *udp_send_thread(void *arg)
{
    socket_handle *sock = arg;

    // Array_Blocking_Queueの何番目に格納したかを示すインデックス
    int abq1_index = 0;

    int i = 0;

    // Actual streaming
    while (running)
    {
        // キューから取り出し
        if (blocking_queue_take(&abq1, &abq1_index))
        {
            printf("Take stream data error.\n");
            break;
        }

        // Send stream data
        if (sendto(sock->sockfd, stream_buffer[abq1_index].samples, NUM_SAMPS_PER_ONCE * 2 * sizeof(int16_t), 0, (struct sockaddr *)&sock->server_addr, sizeof(sock->server_addr)) < 0)
        {
            perror("UDP send failed\n");
            break;
        }

        printf("Send: %d\n", i);
        i++;
    }

    // Stop
    pthread_mutex_lock(&mutex);
    running = 0;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int socket_close(socket_handle sock)
{
    // Close socket
    close(sock.sockfd);

    return 0;
}