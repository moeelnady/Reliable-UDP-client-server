#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#define MYPORT "8080"
#define MSS 500

typedef enum
{
    SLOW_START,
    FAST_RECOVERY,
    CONGESTION_AVOIDANCE
} state;

struct packet
{
    uint16_t cksum;
    uint32_t len;
    uint32_t seqno;
    char data[500];
};

struct ack_packet
{
    uint16_t cksum;
    uint32_t len;
    uint32_t ackno;
};

void prinf_then_exit(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    vprintf(message, args);
    va_end(args);
    exit(0);
}

int main(int argc, char const *argv[])
{
    printf("starting the server..\n");

    int socket_fd;                          /* the socket file descriptor of this client*/
    struct addrinfo hints, *serverinfo, *p; /* will be used to get the initial sockaddr of the server */

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, MYPORT, &hints, &serverinfo) != 0)
    {
        prinf_then_exit("problem with getaddrinfo()");
    }

    for (p = serverinfo; p != NULL; p = p->ai_next)
    {
        if ((socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("error when creating the socket");
            continue;
        }
        if (bind(socket_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(socket_fd);
            perror("error when binding");
            continue;
        }
        break;
    }

    if (p == NULL)
    {
        prinf_then_exit("failed to create socket or bind");
    }
    printf("server started, waiting for requests \n");
    struct packet pkt;
    struct ack_packet ack;
    while (1)
    {
        struct sockaddr_in clientaddr;                /* the client sockaddr_in that we will be communicating over for the rest of the session */
        socklen_t clientaddr_len = sizeof clientaddr; /* the size of clientaddr will be filled by recvfrom*/

        int status = recvfrom(socket_fd, &pkt, sizeof pkt, 0, &clientaddr, &clientaddr_len);
        printf("recieved request for file %s\n", pkt.data);
        FILE *requested_file = fopen(pkt.data, "rb");
        if (requested_file == NULL)
        {
            // meaning the requested file doesn't exist
            printf("file %s doesn't exist", pkt.data);
            ack.ackno = 0;
            ack.len = 0;
            sendto(socket_fd, &ack, sizeof ack, 0, &clientaddr, clientaddr_len);
        }
        else
        {

            fseek(requested_file, 0, SEEK_END);
            size_t file_size = ftell(requested_file);
            fseek(requested_file, 0, SEEK_SET);
            int total_number_of_packets = file_size / MSS;
            if (file_size % MSS != 0)
                total_number_of_packets++;
            struct packet packets_buffer[total_number_of_packets];
            for (int i = 0; i < total_number_of_packets; i++)
            {
                fread(packets_buffer[i].data, 1, 500, requested_file);
            }
            state current_state = SLOW_START;
        }
    }
}