#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <complex.h>

#include "array_blocking_queue_integer.h"
#include "usrp.h"
#include "udp_send.h"

// ---------------------Status---------------------
extern sig_atomic_t running;
extern pthread_mutex_t mutex;
// ------------------------------------------------

// --------------From USRP Streaming---------------
extern size_t num_samps_per_once;

/*
extern stream_data_t *stream_buffer;
extern Array_Blocking_Queue_Integer abq1;
*/
// ------------------------------------------------

// -----------From Polyphase Channelizer-----------
extern unsigned int num_channels;

extern float complex *channelizer_output;
extern Array_Blocking_Queue_Integer abq2;
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

    // Array_Blocking_Queue（abq1）の何番目に格納したかを示すインデックス
    // int abq1_index = 0;

    // Array_Blocking_Queue（abq2）の何番目に格納したかを示すインデックス
    int abq2_index = 0;

    // チャネライズ後の1チャネルあたりのサンプル数
    unsigned int num_frames = num_samps_per_once / num_channels;

    // 送信データ用バッファ
    float send_buf[num_samps_per_once * 2];

    // 送信データサイズ
    int send_size = 0;

    int i = 0;

    // Actual streaming
    while (running)
    {
        /*
        // キューから取り出し
        if (blocking_queue_take(&abq1, &abq1_index))
        {
            printf("Take stream data error.\n");
            break;
        }

        // Send stream data
        if (sendto(sock->sockfd, (unsigned char*)stream_buffer[abq1_index].samples, num_samps_per_once * 2 * sizeof(int16_t), 0, (struct sockaddr *)&sock->server_addr, sizeof(sock->server_addr)) < 0)
        {
            perror("UDP send failed\n");
            break;
        }
        */

        // キューから取り出し
        if (blocking_queue_take(&abq2, &abq2_index))
        {
            printf("Take channelizer output error.\n");
            break;
        }

        // float complexからfloatに変換
        //
        // --------------------------------
        // チャネライザの出力の並べ替えを行い、UDP送信用のバッファに格納する
        //
        // a_0, b_0, c_0, d_0, e_0, ..., a_1, b_1, c_1, d_1, e_1, ...
        // ↓
        // a_0, a_1, a_2, ..., a_n, b_0, b_1, b_2, ..., b_n, c_0, ...
        // --------------------------------
        //
        for (int i = 0; i < (int)num_frames; i++)
        {
            for (int j = 0; j < (int)num_channels; j++)
            {
                send_buf[(j * num_frames * 2) + (i * 2)] = crealf(channelizer_output[abq2_index * num_samps_per_once + i * num_channels + j]);
                send_buf[(j * num_frames * 2) + (i * 2) + 1] = cimagf(channelizer_output[abq2_index * num_samps_per_once + i * num_channels + j]);
            }
        }

        send_size = sizeof(send_buf);

        // Send channelizer output
        if (sendto(sock->sockfd, (unsigned char *)send_buf, send_size, 0, (struct sockaddr *)&sock->server_addr, sizeof(sock->server_addr)) < 0)
        {
            perror("UDP send failed\n");
            break;
        }

        printf("Send: %d[byte],  %d,  %f\n", send_size, i, send_buf[100]);
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