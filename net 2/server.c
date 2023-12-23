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
#include <time.h>
#define MYPORT "8080"
#define MSS 500

enum state
{
    SLOW_START,
    FAST_RECOVERY,
    CONGESTION_AVOIDANCE
};

typedef enum state STATE;
typedef struct packet PACKET;
typedef struct ack_packet ACK;

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
int getProbability(double plp)
{
    srand(time(NULL));
    return (rand() % 100) < ((1 - plp) * 100);
}

int main(int argc, char const *argv[])
{
    if (argc != 4)
    {
        prinf_then_exit("%d", (rand() % 100) < ((1 - 0.99) * 100));
    }
    int seed = atoi(argv[2]);
    double plp = atof(argv[3]);

    printf("starting the server..\n");

    int socket_fd;                          /* the socket file descriptor of this client*/
    struct addrinfo hints, *serverinfo, *p; /* will be used to get the initial sockaddr of the server */

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, argv[1], &hints, &serverinfo) != 0)
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
    PACKET pkt;
    ACK ack;
    while (1)
    {
        struct sockaddr_in clientaddr;                /* the client sockaddr_in that we will be communicating over for the rest of the session */
        socklen_t clientaddr_len = sizeof clientaddr; /* the size of clientaddr will be filled by recvfrom*/

        int status = recvfrom(socket_fd, &pkt, sizeof pkt, 0, &clientaddr, &clientaddr_len);
        printf("received request for file %s\n", pkt.data);
        int pid = fork();
        if (pid == 0)
        {
            socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket_fd == -1)
            {
                prinf_then_exit("error when creating client socket\n");
            }
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

                ack.ackno = 0;
                ack.len = total_number_of_packets;
                sendto(socket_fd, &ack, sizeof ack, 0, &clientaddr, clientaddr_len);

                PACKET buff[total_number_of_packets]; /* a buffer containing all packets to be sent*/
                STATE current_state = SLOW_START;     /* the current state, initially SLOW_START */
                uint32_t cwnd = MSS;                  /* current congestion window size, initially 1 MSS */
                uint32_t ssthresh = 64 * 1024;        /* current ssthresh, initially 64 KB */
                int dupACKcount = 0;                  /* duplicate ack count, initially 0 */
                int next = 0;                         /* the next packet to send*/
                uint32_t last_byte_acked = 0;         /* the seqno of the last acked byte, initially 0 */
                uint32_t last_byte_sent = 0;          /* the seqno of the last sent byte */
                ACK ack_pkt;                          /* where we will store the received ack */
                int finished = 0;                     /* boolean indicating if we finished */

                for (int i = 0; i < total_number_of_packets; i++)
                {
                    // here we setup the packets
                    // the first packet will start with seqno 1

                    size_t len = fread(buff[i].data, 1, 500, requested_file);
                    buff[i].len = len;
                    if (i == 0)
                    {
                        buff[i].seqno = 1;
                    }
                    else
                    {
                        buff[i].seqno = buff[i - 1].seqno + buff[i - 1].len;
                    }
                }
                fclose(requested_file);
                struct timeval timeout;
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;
                setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                FILE *details = fopen("details.txt", "w");
                if (details == NULL)
                {
                    prinf_then_exit("failed to creat details file");
                }
                clock_t begin = clock();
                while (1)
                {
                    if (finished)
                    {
                        return 0;
                    }
                    while (current_state == SLOW_START)
                    {
                        // here we sent packets as long it is within the current cwnd
                        while (last_byte_sent - last_byte_acked <= cwnd && next < total_number_of_packets)
                        {
                            if (getProbability(plp))
                            {
                                ssize_t x = sendto(socket_fd, &(buff[next]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                                printf("send packet with seqno = %u\n", buff[next].seqno);
                            }
                            else
                            {
                                printf("packet with seqno = %u is lost\n", buff[next].seqno);
                            }
                            fprintf(details, "%u, %u\n", cwnd, ssthresh);
                            last_byte_sent = buff[next].seqno + buff[next].len - 1;
                            next++;
                        }
                        if (recvfrom(socket_fd, &ack_pkt, sizeof(ack_pkt), 0, &clientaddr, &clientaddr_len) == -1)
                        {
                            printf("timeout, last acked byte was %u\n", last_byte_acked);
                            ssthresh = cwnd / 2;
                            cwnd = MSS;
                            dupACKcount = 0;
                            if (getProbability(plp))
                            {
                                printf("retransmitting packet with seqno = %u\n", buff[(last_byte_acked + 1) / MSS].seqno);
                                sendto(socket_fd, &(buff[(last_byte_acked + 1) / MSS]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                            }
                            else
                            {
                                printf("retransmisson for packet with seqno %u lost\n", buff[(last_byte_acked + 1) / MSS].seqno);
                            }
                            fprintf(details, "%u, %u\n", cwnd, ssthresh);
                            break;
                        }
                        else if (ack_pkt.ackno - 1 <= last_byte_acked)
                        {
                            printf("received a duplicate ack with ackno = %u\n", ack_pkt.ackno);
                            dupACKcount++;

                            if (dupACKcount == 3)
                            {
                                current_state = FAST_RECOVERY;
                                ssthresh = cwnd / 2;
                                cwnd = ssthresh + 3 * MSS;
                                if (getProbability(plp))
                                {
                                    printf("retransmitting packet with seqno = %u\n", buff[ack_pkt.ackno / MSS].seqno);
                                    sendto(socket_fd, &(buff[ack_pkt.ackno / MSS]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                                }
                                else
                                {
                                    printf("retransmisson for packet with seqno %u lost\n", buff[ack_pkt.ackno / MSS].seqno);
                                }
                                fprintf(details, "%u, %u\n", cwnd, ssthresh);
                                break;
                            }
                        }
                        else
                        {
                            printf("received ack with ackno = %u\n", ack_pkt.ackno);
                            if (ack_pkt.len == 0)
                            {
                                finished = 1;
                                break;
                            }
                            last_byte_acked = ack_pkt.ackno - 1;
                            cwnd += MSS;
                            dupACKcount = 0;
                            printf("\nwindow size increased is now %u bytes\n", cwnd);
                            if (cwnd >= ssthresh)
                            {
                                current_state = CONGESTION_AVOIDANCE;
                                break;
                            }
                        }
                    }
                    while (current_state == CONGESTION_AVOIDANCE)
                    {
                        while (last_byte_sent - last_byte_acked <= cwnd && next < total_number_of_packets)
                        {
                            if (getProbability(plp))
                            {
                                ssize_t x = sendto(socket_fd, &(buff[next]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                                printf("send packet with seqno = %u\n", buff[next].seqno);
                            }
                            else
                            {
                                printf("packet with seqno = %u is lost\n", buff[next].seqno);
                            }
                            fprintf(details, "%u, %u\n", cwnd, ssthresh);
                            last_byte_sent = buff[next].seqno + buff[next].len - 1;
                            next++;
                        }
                        if (recvfrom(socket_fd, &ack_pkt, sizeof(ack_pkt), 0, &clientaddr, &clientaddr_len) == -1)
                        {
                            printf("timeout, last acked byte was %u\n", last_byte_acked);
                            ssthresh = cwnd / 2;
                            if (ssthresh < MSS)
                                ssthresh = MSS;
                            cwnd = MSS;
                            dupACKcount = 0;
                            if (getProbability(plp))
                            {
                                printf("retransmitting packet with seqno = %u\n", buff[(last_byte_acked + 1) / MSS].seqno);
                                sendto(socket_fd, &(buff[(last_byte_acked + 1) / MSS]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                            }
                            else
                            {
                                printf("retransmisson for packet with seqno %u lost\n", buff[(last_byte_acked + 1) / MSS].seqno);
                            }
                            fprintf(details, "%u, %u\n", cwnd, ssthresh);
                            current_state = SLOW_START;
                            break;
                        }
                        else if (ack_pkt.ackno - 1 <= last_byte_acked)
                        {
                            printf("received a duplicate ack with ackno = %u\n", ack_pkt.ackno);
                            dupACKcount++;

                            if (dupACKcount == 3)
                            {
                                current_state = FAST_RECOVERY;
                                ssthresh = cwnd / 2;
                                if (ssthresh < MSS)
                                    ssthresh = MSS;
                                cwnd = ssthresh + 3 * MSS;
                                if (getProbability(plp))
                                {
                                    printf("retransmitting packet with seqno = %u\n", buff[ack_pkt.ackno / MSS].seqno);
                                    sendto(socket_fd, &(buff[ack_pkt.ackno / MSS]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                                }
                                else
                                {
                                    printf("retransmisson for packet with seqno %u lost\n", buff[ack_pkt.ackno / MSS].seqno);
                                }
                                fprintf(details, "%u, %u\n", cwnd, ssthresh);
                                break;
                            }
                        }
                        else
                        {
                            printf("received ack with ackno = %u\n", ack_pkt.ackno);
                            if (ack_pkt.len == 0)
                            {
                                finished = 1;
                                break;
                            }
                            last_byte_acked = ack_pkt.ackno - 1;
                            cwnd += MSS * (MSS / cwnd);
                            dupACKcount = 0;
                            printf("\nwindow size increased is now %u bytes\n", cwnd);
                        }
                    }
                    while (current_state == FAST_RECOVERY)
                    {
                        while (last_byte_sent - last_byte_acked <= cwnd && next < total_number_of_packets)
                        {
                            if (getProbability(plp))
                            {
                                ssize_t x = sendto(socket_fd, &(buff[next]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                                printf("send packet with seqno = %u\n", buff[next].seqno);
                            }
                            else
                            {
                                printf("packet with seqno = %u is lost\n", buff[next].seqno);
                            }
                            fprintf(details, "%u, %u\n", cwnd, ssthresh);
                            last_byte_sent = buff[next].seqno + buff[next].len - 1;
                            next++;
                        }
                        if (recvfrom(socket_fd, &ack_pkt, sizeof(ack_pkt), 0, &clientaddr, &clientaddr_len) == -1)
                        {
                            printf("timeout, last acked byte was %u\n", last_byte_acked);
                            ssthresh = cwnd / 2;
                            if (ssthresh < MSS)
                                ssthresh = MSS;
                            cwnd = MSS;
                            dupACKcount = 0;
                            if (getProbability(plp))
                            {
                                printf("retransmitting packet with seqno = %u\n", buff[(last_byte_acked + 1) / MSS].seqno);
                                sendto(socket_fd, &(buff[(last_byte_acked + 1) / MSS]), sizeof(PACKET), 0, &clientaddr, clientaddr_len);
                            }
                            else
                            {
                                printf("retransmisson for packet with seqno %u lost\n", buff[(last_byte_acked + 1) / MSS].seqno);
                            }
                            current_state = SLOW_START;
                            break;
                        }
                        else if (ack_pkt.ackno - 1 <= last_byte_acked)
                        {
                            printf("received a duplicate ack with ackno = %u\n", ack_pkt.ackno);
                            dupACKcount++;
                            cwnd += MSS;
                            printf("\nwindow size is now %u bytes\n", cwnd);
                        }
                        else
                        {
                            printf("received ack with ackno = %u\n", ack_pkt.ackno);
                            if (ack_pkt.len == 0)
                            {
                                finished = 1;
                                break;
                            }
                            last_byte_acked = ack_pkt.ackno - 1;
                            cwnd = ssthresh;
                            dupACKcount = 0;
                            current_state = CONGESTION_AVOIDANCE;
                            printf("\nwindow size is now %u bytes\n", cwnd);
                        }
                    }
                }
                clock_t end = clock();
                printf("time %lf\n", (double)((end - begin) / CLOCKS_PER_SEC));
                fclose(details);
                return 0;
            }
        }
        else if (pid == -1)
        {
            prinf_then_exit("error when fork()");
        }
    }
}