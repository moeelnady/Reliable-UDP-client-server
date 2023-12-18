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

void exitWithMessage(const char *message)
{
    printf("\n%s", message);
    exit(0);
}
int main(int argc, char const *argv[])
{

    if (argc != 4)
    {
        exitWithMessage("Arguments should be in the from [server-ip] [sever-port] [filename]");
    }

    int socketfd;                           /* the socket file descriptor of this client*/
    struct addrinfo hints, *serverinfo, *p; /* will be used to get the initial sockaddr of the server*/
    struct sockaddr_in serveraddr;          /* the server sockaddr_in that we will be communicating over for the rest of the session */
    socklen_t serveraddr_len;               /* the size of serveraddr will be filled by recvfrom*/

    memset(&serveraddr, 0, sizeof serveraddr);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(argv[1], argv[2], &hints, &serverinfo) != 0)
    {
        exitWithMessage("problem with getaddrinfo()");
    }

    for (p = serverinfo; p != NULL; p = p->ai_next)
    {
        if ((socketfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("failed to create socket");
            continue;
        }
        break;
    }

    if (p == NULL)
    {
        exitWithMessage("failed to create socket");
    }
    printf("%s\n", inet_ntoa(((struct sockaddr_in *)p->ai_addr)->sin_addr));
    printf("%d\n", (((struct sockaddr_in *)p->ai_addr)->sin_port));

    struct packet fileRequestPacket;
    fileRequestPacket.seqno = 0;
    fileRequestPacket.len = strlen(argv[3]) + 1;
    memcpy(fileRequestPacket.data, argv[3], fileRequestPacket.len);

    struct ack_packet response;

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int sendStatus;
    int recvStatus;

    while (1)
    {

        sendStatus = sendto(socketfd, &fileRequestPacket, sizeof(fileRequestPacket), 0, p->ai_addr, p->ai_addrlen);
        printf("%d bytes was sent\n", sendStatus);

        recvStatus = recvfrom(socketfd, &response, sizeof(response), 0, &serveraddr, &serveraddr_len);

        if (recvStatus == sizeof(struct packet))
        {
            break;
        }

        printf("timeout, request will be resent\n");
    }

    if (response.ackno == -1)
    {
        printf("server doesn't have the requested file");
    }
}