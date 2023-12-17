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
#define MSS 499
#define AckPacketSize 8
#define maxSegSize  508
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


void sendAckFileName(int client_fd,string fName,int numberOfPackets,struct sockaddr_in client_addr){
    struct ack_packet ack;
    ack.cksum=0;
    ack.len=numberOfPackets;
    ack.ackno=0;
    char* buffer = new char[maxSegSize];
    memset(buffer, 0, maxSegSize);
    memcpy(buffer, &ack, sizeof(ack));
    ssize_t bytesSent = sendto(client_fd, buffer, maxSegSize, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("Error Sending The Ack ! ");
        exit(1);
    } else {
        printf("Ack of file name is sent successfully\n");
    }


    /*
        get data from file sent by client
    */
   FILE *file = fopen(&fName[0], "rb"); // Open the file in binary mode
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    char buffer[MSS];
    size_t bytesRead;
    size_t totalBytesRead = 0;
    char *response = NULL;
    size_t responseSize = 0;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        // Resize the response buffer
        response =(char *)realloc(response, responseSize + bytesRead);
        if (response == NULL) {
            perror("Error allocating memory");
            fclose(file);
            return;
        }
        // Copy the read data to the response buffer
        memcpy(response + responseSize, buffer, bytesRead);
        responseSize += bytesRead;
        totalBytesRead += bytesRead;
    }

    ///TODO:*******SEND DATA AND HANDLE CONGESTION**********************************************




}
void handleClientRequest(int serverSocket, int client_fd, struct sockaddr_in client_addr, char rec_buffer [] , int bufferSize){
    auto* data_packet = (struct packet*) rec_buffer;
    string fileName = string(data_packet->data);
    printf("the file name that the client wants is : %s\n", fileName.c_str());
    printf("with size: %zu \n",fileName.size());
    int fileSize = getFileSize(fileName.c_str());
    if (fileSize == -1){
        return;
    }
    int numberOfPackets = ceil(fileSize * 1.0 / MSS);
    printf("File Size : %ld Bytes , Num. of chuncks : %d\n", fileSize, numberOfPackets);
    
    ///TODO: SEND AN ACK()**************************************************************

    
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
             //handle_client_request(serverSocket,clientSocket, clientAddress, buffer , maxSegSize);
             return 6;
         }


    }
 

    close(serverSocket);
    printf("Hello, World!\n");
    return 0;
}
