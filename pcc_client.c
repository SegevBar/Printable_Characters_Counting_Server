#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#define MAX_BATCH_SIZE 1048576
#define min(X, Y) (((X) < (Y)) ? (X) : (Y))

/* prototypes */
int send_data_to_server(int sockfd, char* buffer, int count);
int read_data_from_server(int sockfd, char* buffer, int count);


int main(int argc, char const *argv[]) {
    const char* IP_address;
    int server_port;
    const char* file_path;
    FILE* fd;
    struct sockaddr_in serv_addr; 
    uint32_t N;
    uint32_t endian_N;
    int batch_size;
    int bytes_left;
    char* batch_file_buffer;
    uint32_t C;
    uint32_t endian_C;
    char* recv_buff;
    int sockfd = -1;

    if (argc != 4) {
        perror("ERROR: invalid number of arguments");
        exit(1);
    }
    
    /* get cmd arguments */
    IP_address = argv[1];
    server_port = atoi(argv[2]);
    file_path = argv[3];

    /* open given file */
    fd = fopen(file_path, "r+");
    if (fd == NULL) {
        perror("ERROR: failed to open given file");
        exit(1);
    }
    /* find given file total size 
    https://www.tutorialspoint.com/c_standard_library/c_function_ftell.htm */
    fseek(fd, 0L, SEEK_END);
    N = ftell(fd);
    fseek(fd, 0L, SEEK_SET);

    /* create a TCP connection (code from rec10) */
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR: Could not create socket \n");
        exit(1);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port); 
    inet_pton(AF_INET, IP_address, &serv_addr.sin_addr.s_addr);  /*converting a string IP address to binary representation*/

    /* connect */
    if (connect(sockfd,
              (struct sockaddr*) &serv_addr,
               sizeof(serv_addr)) < 0) {
        perror("ERROR: Connect Failed");
        exit(1);
    }

    /* send N - given file size - to server */
    endian_N = htonl(N);
    send_data_to_server(sockfd, (char*) &endian_N, 4); /*32bits == 4bytes*/

    /* send given file data in batches */
    bytes_left = N;

    while (bytes_left > 0) {
        /* create a buffer of the maximal amount of data from given file */
        batch_size = min(bytes_left, MAX_BATCH_SIZE);

        batch_file_buffer = (char*) malloc(batch_size*sizeof(char));
        if (batch_file_buffer == NULL) {
            perror("ERROR: an error accured while allocating memory to batch file buffer");
            exit(1);
        }
        if (fread(batch_file_buffer, sizeof(char), batch_size, fd) < batch_size) {
            perror("ERROR: an error accured while reading bytes from given file");
            exit(1);
        }

        /* send current batch file buffer to server */
        send_data_to_server(sockfd, batch_file_buffer, batch_size);
        free(batch_file_buffer);

        bytes_left -= batch_size;
    }
    fclose(fd);

    /* get C from server */
    recv_buff = (char*) &endian_C;
    read_data_from_server(sockfd, recv_buff, 4);
    C = ntohl(endian_C);
    
    printf("# of printable characters: %u\n", C);
    
    close(sockfd);
    return 0;
}

/*
* write to socket fd iterativly and returns the number of bytes written
*
*/
int send_data_to_server(int sockfd, char* buffer, int count) {
    int bytes_sent = 1;
    int totalsent = 0;

    /* iterate until all buffer bytes are written to socket fd */
    while (count > 0) {
        bytes_sent = write(sockfd, buffer+totalsent, count);

        if (bytes_sent < 0) {
            perror("ERROR: an error accured while writing to server");
            exit(1);
        }
        totalsent += bytes_sent;
        count -= bytes_sent;
    }

    return totalsent;
}

/*
* read from socket fd iterativly and returns the number of bytes read
*
*/
int read_data_from_server(int sockfd, char* buffer, int count) {
    int bytes_read = 1;
    int totalread = 0;
    
    while (bytes_read > 0) {  /* C is 32bits == 4bytes*/
        bytes_read = read(sockfd, buffer+totalread, count);
        if (bytes_read < 0) {
            perror("ERROR: an error accured while reading bytes from server");
            exit(1);
        }
        totalread += bytes_read;
        count -= bytes_read;
    }

    return totalread;
}

