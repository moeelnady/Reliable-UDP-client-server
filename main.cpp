#include <stdio.h>
#include <sys/socket.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <vector>
#include <string>
#include <fstream>
#include <unistd.h>
#include <math.h>
#define PORT 8080
#define MSS 500
#define AckPacketSize 8
#define maxSegSize  508
#define TIMEOUT_SECONDS 5;
using namespace std;

int randomSeed;
double plp;

 struct packet {
	uint16_t cksum;
	uint32_t len;
	uint32_t seqno;
	char data[500];
};

struct ack_packet {
	uint16_t cksum;
	uint32_t len;
	uint32_t ackno;
};
/*
    read info from file
*/
vector<string> readInfo()
{
    string fName = "info.txt";
    vector<string> reqs;
    string line;
    ifstream f;
    f.open(fName);
    while(getline(f, line))
    {
        reqs.push_back(line);
    }
    return reqs;
}
long getFileSize(const char *filename) {
    // Open the file in binary mode
    FILE *file = fopen(filename, "rb");

    // Check if the file exists
    if (file == NULL) {
        // File does not exist
        return -1;
    }
    printf("file opened successfully\n");
    // Seek to the end of the file to get its size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);

    // Close the file
    fclose(file);

    return size;
}
bool corruptDatagram()
{
    double isLost = (rand() % 100) * plp;
    if (isLost >= 5.9)
    {
        return true;
    }
    return false;
}
int timeOut(int sockfd) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    // Set up the timeout
    struct timeval timeout{};
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    // Wait for the socket to become readable or for the timeout to expire
    int status = select(sockfd + 1, &read_fds, nullptr, nullptr, &timeout);

    return status;
}
///stop and wait

void sendDataPackets(int client_fd, struct sockaddr_in client_addr ,struct packet packets[],int numberOfPackets){
    //char sendBuffer [maxSegSize];
    //memset(sendBuffer, 0, maxSegSize);
    for(int i=0;i<numberOfPackets;i++){
        //memcpy(sendBuffer, &packets[i], sizeof(packets[i]));
        if (!corruptDatagram()){
            size_t bytesSent = sendto(client_fd, &packets[i], maxSegSize, 0, (struct sockaddr *)&client_addr,
                                     sizeof(struct sockaddr));
            if (bytesSent == -1) {
                printf("failed to send a packet");
                return;
            }
            ///wait to recieve ack until a certain time then timeout
            int status =timeOut(client_fd);
            if (status == -1) {
            // An error occurred
            printf("Error waiting for socket\n");
            return ;
            }
            else if (status == 0) {
                printf("Timeout Expired\n");
                i--;
            }
            else{
                struct ack_packet ack;
                socklen_t ClientAddressLength =sizeof(struct sockaddr);
                long bytes_received = recvfrom(client_fd, &ack, AckPacketSize, 0,
                                         (struct sockaddr*)&client_addr,&ClientAddressLength);
                printf("Ack is recieved for packet with ackno : %d\n",ack.ackno);
            }

        }
        else{
            printf("data is corrupted , will be resent again\n");
            i--;
            
        }

    }
    
    close(client_fd);
    
    return;
}

void sendAckFileName(int client_fd,string fName,int numberOfPackets,struct sockaddr_in client_addr){
    struct ack_packet ack;
    ack.cksum=0;
    ack.len=numberOfPackets;
    ack.ackno=0;
    //char* buffer = new char[maxSegSize];
    //memset(buffer, 0, maxSegSize);
    //memcpy(buffer, &ack, sizeof(ack));
    ssize_t bytesSent = sendto(client_fd, &ack, AckPacketSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("Error Sending The Ack !");
        exit(1);
    } else {
        printf("Ack of file name is sent successfully\n");
    }


    /*
        get data from file name sent by client
    */
   FILE *file = fopen(&fName[0], "rb"); // Open the file in binary mode
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    char data_buffer[MSS];
    size_t bytesRead;
    size_t totalBytesRead = 0;
    char *response = NULL;
    size_t responseSize = 0;
    struct packet packets_to_be_sent[numberOfPackets];
    int i=0;
    while (((bytesRead = fread(data_buffer, 1, sizeof(data_buffer), file)) > 0) && i<numberOfPackets) {
            
            strcpy(packets_to_be_sent[i].data,data_buffer);
            packets_to_be_sent[i].len=bytesRead;
            i++;
        // Resize the response buffer
        response =(char *)realloc(response, responseSize + bytesRead);
        if (response == NULL) {
            perror("Error allocating memory");
            fclose(file);
            return;
        }
        // Copy the read data to the response buffer
        memcpy(response + responseSize, data_buffer, bytesRead);
        responseSize += bytesRead;
        totalBytesRead += bytesRead;
    }

    ///TODO:*******SEND DATA AND HANDLE CONGESTION**********************************************

    sendDataPackets(client_fd, client_addr, packets_to_be_sent,numberOfPackets);


}
void handleClientRequest(int serverSocket, int client_fd, struct sockaddr_in client_addr, char rec_buffer [] , int bufferSize){
    auto* data_packet = (struct packet*) rec_buffer;
    string fileName = string(data_packet->data);
    printf("the file name that the client wants is : %s\n", fileName.c_str());
    //printf("with size: %zu \n",fileName.size());
    long fileSize = getFileSize(fileName.c_str());
    printf("file size is : %ld\n",fileSize);
    if (fileSize == -1){
        return;
    }
    int numberOfPackets = (fileSize+MSS-1)/MSS;
    printf("File Size : %ld Bytes , Num. of chuncks : %d\n", fileSize, numberOfPackets);
    
    ///TODO: SEND AN ACK()**************************************************************
    sendAckFileName(client_fd,fileName,numberOfPackets,client_addr);

    
}



int main(int argc, char const* argv[]) {

     vector<string> args = readInfo();
    int portNo = stoi(args[0]);
    randomSeed = stoi(args[1]);
    srand(randomSeed);
    plp = stod(args[2]);

    int serverSocket, clientSocket;
    struct sockaddr_in serverAddress, clientAddress;
    int server_addrlen = sizeof(serverAddress);
    if ((serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        printf("Creating server socket failure\n");
        return 1;
    }
    memset(&serverAddress, 0, sizeof(serverAddress));
    memset(&clientAddress, 0, sizeof(clientAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNo);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    memset(&(serverAddress.sin_zero), '\0', AckPacketSize);
    

    if (bind(serverSocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
    {
        printf("Binding server failure\n");
        return 2;
    }
    while (true)
    {
        socklen_t clientAddressLength = sizeof(struct sockaddr);
        printf("Ready for Connection ...\n");
        char buffer[maxSegSize];
        ssize_t bytesRead = recvfrom(serverSocket, buffer, maxSegSize, 0, (struct sockaddr*)&clientAddress, &clientAddressLength);
        if(bytesRead <= 0)
        {
            printf("Receiving file bytes failure\n");
            return 3;
        }
        pid_t pid = fork();
        if (pid == -1)
        {
            printf("Failed to fork child process for the client\n");
            return 4;
        }
        else if (pid == 0)
        {
            if ((clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            {
                printf("Failed to create client socket\n");
                return 5;
            }
            handleClientRequest(serverSocket,clientSocket, clientAddress, buffer , maxSegSize);
            return 6;
         }

    }
 

    close(serverSocket);
    printf("Hello, World!\n");
    return 0;
}
