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
    if (argc != 4)
    {
        prinf_then_exit("Arguments should be in the from [server-ip] [sever-port] [filename]");
    }

    int socket_fd;                                /* the socket file descriptor of this client*/
    struct addrinfo hints, *p_serverinfo, *p;     /* will be used to get the initial sockaddr of the server*/
    struct sockaddr_in serveraddr;                /* the server sockaddr_in that we will be communicating over for the rest of the session */
    socklen_t serveraddr_len = sizeof serveraddr; /* the size of serveraddr will be filled by recvfrom*/

    memset(&serveraddr, 0, sizeof serveraddr);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(argv[1], argv[2], &hints, &p_serverinfo) != 0)
    {
        prinf_then_exit("problem with getaddrinfo()");
    }

    for (p = p_serverinfo; p != NULL; p = p->ai_next)
    {
        if ((socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("failed to create socket");
            continue;
        }
        break;
    }

    if (p == NULL)
    {
        prinf_then_exit("failed to create socket");
    }

    struct packet file_request_packet;
    file_request_packet.seqno = 0;
    file_request_packet.len = strlen(argv[3]) + 1;
    memcpy(file_request_packet.data, argv[3], file_request_packet.len);

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int sendStatus;
    int recvStatus;

    struct ack_packet response;
    while (1)
    {

        sendStatus = sendto(socket_fd, &file_request_packet, sizeof(file_request_packet), 0, p->ai_addr, p->ai_addrlen);
        printf("%d bytes was sent\n", sendStatus);

        recvStatus = recvfrom(socket_fd, &response, sizeof(response), 0, (struct sockaddr *)&serveraddr, &serveraddr_len);

        if (recvStatus != -1)
        {
            break;
        }

        printf("timeout, request will be resent\n");
    }

    if (response.ackno == 0)
    {
        prinf_then_exit("\nserver doesn't have %s", argv[3]);
    }

    timeout.tv_sec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    uint32_t total_number_of_packets = response.len;
    uint32_t total_received = 0;
    uint32_t expected_seqno = 0;

    struct packet pkt;
    struct ack_packet ack = {0, sizeof(struct ack_packet), 0};

    FILE *file = fopen(argv[3], "wb");
    if (file == NULL)
    {
        prinf_then_exit("error when creating the file");
    }

    while (total_received != total_number_of_packets)
    {
        recvfrom(socket_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&serveraddr, &serveraddr_len);
        if (expected_seqno != pkt.seqno)
        {
            ack.ackno = pkt.seqno;
            sendto(socket_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serveraddr, serveraddr_len);
        }
        else
        {
            ack.ackno = expected_seqno;
            sendto(socket_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serveraddr, serveraddr_len);
            if (fwrite(pkt.data, 1, pkt.len, file) != pkt.len)
            {
                prinf_then_exit("error when writing to file");
            }
            total_received++;
            expected_seqno = !expected_seqno;
        }
    }
    fclose(file);
    freeaddrinfo(p_serverinfo);
    close(socket_fd);
}