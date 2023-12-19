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
/**
 * @brief a function that sends a message using sendto and exits if there is an error
 * @param socket the sending socket
 * @param buffer a pointer to the buffer that contains the data where sending
 * @param size the size of the data to be sent (smaller or equal to the buffer)
 * @param flags flags that are passed to sendto
 * @param addr pointer to the sockaddr of the receiver
 * @param length the length of the receiver sockaddr
 * @return number of bytes send
 */
ssize_t send_message(int socket_fd, const void *buffer, size_t size, int flags, struct sockaddr *addr, socklen_t length)
{
    ssize_t count = sendto(socket_fd, buffer, size, flags, addr, length);
    if (count == -1)
    {
        prinf_then_exit("error when sending");
    }
    return count;
}

/**
 * @brief a function that receives a message using recvfrom
 * @param socket the receiving socket
 * @param buffer a pointer to the buffer where the message will be stored
 * @param size the size of the data to be received (smaller or equal to the buffer)
 * @param flags flags that are passed to recvfrom
 * @param addr pointer to the sockaddr of the sender (will be filled by recvfrom)
 * @param length the length of the sender sockaddr (will be filled by recvfrom)
 * @param exit exit or not if recvfrom returns -1
 * @return number of bytes received
 */
ssize_t recv_message(int socket_fd, void *buffer, size_t size, int flags, struct sockaddr *addr, socklen_t *length, int exit)
{
    ssize_t count = recvfrom(socket_fd, buffer, size, flags, addr, length);
    if (count == -1 && exit)
    {
        prinf_then_exit("error when sending");
    }
    return count;
}

int main(int argc, char const *argv[])
{
    if (argc != 4)
    {
        prinf_then_exit("Arguments should be in the from [server-ip] [sever-port] [filename]");
    }

    int socket_fd;                            /* the socket file descriptor of this client*/
    struct addrinfo hints, *p_serverinfo, *p; /* will be used to get the initial sockaddr of the server*/
    struct sockaddr_in serveraddr;            /* the server sockaddr_in that we will be communicating over for the rest of the session */
    socklen_t serveraddr_len;                 /* the size of serveraddr will be filled by recvfrom*/

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

    struct ack_packet response;

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int sendStatus;
    int recvStatus;

    while (1)
    {

        sendStatus = send_message(socket_fd, &file_request_packet, sizeof(file_request_packet), 0, p->ai_addr, p->ai_addrlen);
        printf("%d bytes was sent\n", sendStatus);

        recvStatus = recv_message(socket_fd, &response, sizeof(response), 0, (struct sockaddr *)&serveraddr, &serveraddr_len, 0);

        if (recvStatus == sizeof(struct packet))
        {
            break;
        }

        printf("timeout, request will be resent\n");
    }

    if (response.ackno == -1)
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
        recv_message(socket_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&serveraddr, &serveraddr_len, 1);
        if (expected_seqno != pkt.seqno)
        {
            ack.ackno = pkt.seqno;
            send_message(socket_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serveraddr, serveraddr_len);
        }
        else
        {
            ack.ackno = expected_seqno;
            send_message(socket_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serveraddr, serveraddr_len);
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