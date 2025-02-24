#ifndef UDP_SEND_H
#define UDP_SEND_H

#include <sys/socket.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1" // 送信先IPアドレス
#define SERVER_PORT 7124      // 送信先ポート番号

// Socket handle
typedef struct _socket_handle
{
    // File descriptor
    int sockfd;
    // Socket address
    struct sockaddr_in server_addr;
} socket_handle;

int socket_setup(socket_handle *sock);
void *udp_send_thread(void *arg);
int socket_close(socket_handle sock);

#endif