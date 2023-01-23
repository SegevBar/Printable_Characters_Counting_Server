#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#define MAX_BATCH_SIZE 1048576
#define min(X, Y) (((X) < (Y)) ? (X) : (Y))

/* prototypes */
void define_sigint();
void sigint_handler();
int read_data_from_client(int connfd, char* buffer, int count);
int send_data_to_client(int connfd, char* buffer, int count);
void terminate_server();

/* global variables */
uint32_t pcc_total[95]; /* a data structure pcc_total that will count how many times each printable character was observed */
int client_in_process = 0; /* flag to indicate if there is a client the server processes */
int sigint = 0; /* flag to indicate if SIGINT was activated */


int main(int argc, char const *argv[]) {
    int server_port;
    struct sockaddr_in serv_addr;
    uint32_t N;
    uint32_t endian_N;
    int bytes_left;
    int batch_size;
    char* batch_buffer;
    uint32_t curr_pcc_count[95];
    int i;
    uint32_t C;
    uint32_t endian_C;
    socklen_t addrsize = sizeof(struct sockaddr_in);
    int listenfd = -1;
    int connfd = -1;
    int totalread = 0;
    int totalsent = 0;
    int read_error_accured = 0;


    define_sigint();  /* SIGINT handler*/

    if (argc != 2) {
        perror("ERROR: invalid number of arguments");
        exit(1);
    }
    memset(curr_pcc_count, 0, sizeof(pcc_total));

    /* get cmd arguments */
    server_port = atoi(argv[1]);

    /* Listen to incoming TCP connections on the specified server port (code is mainly based on rec10) */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR: an error accured while creating a socket");
        exit(1);
    }
    memset(&serv_addr, 0, addrsize);

    serv_addr.sin_family = AF_INET;
    /* INADDR_ANY = any local machine address */
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(server_port);

    /* enable reusing the port quickly after the server terminate
    helped here: https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        perror("ERROR: an error accured while setsockopt");
        exit(1);
    }

    /* bind */
    if (0 != bind(listenfd,
                 (struct sockaddr*) &serv_addr,
                 addrsize)) {
        perror("ERROR: Bind Failed");
        exit(1);
    }

    /* listen - queue of size 10 */
    if (0 != listen(listenfd, 10)) {
        perror("ERROR: Listen Failed");
        exit(1);
    }

    /* iterativly - accepts a TCP connection */
    while (1) {
        client_in_process = 0;
        read_error_accured = 0;
        /* if SIGINT was sent - don't accept another connection and terminate server */
        if (sigint) {
            terminate_server();
        }

        /* accept */
        connfd = accept(listenfd,
                        NULL,
                        NULL);
        if (connfd < 0) {
            perror("ERROR: Accept Failed");
            exit(1);
        }

        client_in_process = 1; /* mark that a client is connected */
        memset(curr_pcc_count, 0, sizeof(pcc_total));

        totalread = read_data_from_client(connfd, (char*) &endian_N, 4); /*32bits == 4 bytes */

        /* in case the reading error is one of ETIMEDOUT, ECONNRESET, or EPIPE 
        * OR the client was killed unexpectedly - start new client connection */
        if (totalread < 0) {
            close(connfd);
            connfd = -1;
            continue;
        }
        
        /* get N */
        N = ntohl(endian_N);

        /* recieve client data in batches and calculate pcc */            
        bytes_left = N;
        totalread = 0;
        C = 0;
        while (bytes_left > 0) {
            /* create a buffer of the maximal amount of data recieved */
            batch_size = min(bytes_left, MAX_BATCH_SIZE);

            batch_buffer = (char*) malloc(batch_size);
            if (batch_buffer == NULL) {
                perror("ERROR: an error accured while allocating memory to batch buffer");
                exit(1);
            }
            
            totalread = read_data_from_client(connfd, batch_buffer, batch_size);

            /* in case the reading error is one of ETIMEDOUT, ECONNRESET, or EPIPE OR the client was killed unexpectedly - start new client connection */
            if (totalread < 0) {
                close(connfd);
                read_error_accured = 1;
                connfd = -1;
                break;
            }

            /* count pcc for current batch */
            for (i = 0; i < batch_size; i++) {
                if (32 <= batch_buffer[i] && batch_buffer[i] <= 126) {
                    C++;
                    curr_pcc_count[batch_buffer[i]-32]++;
                }
            }
            free(batch_buffer);

            bytes_left -= batch_size;
        }
        /* in case the batch reader while loop break due to reading error - continue to next client connection */
        if (read_error_accured) {
            continue;
        }

        /* send C back to client */
        endian_C = htonl(C);
        totalsent = send_data_to_client(connfd, (char*) &endian_C, 4); /*32bits == 4bytes*/

        /* in case the sending error is one of ETIMEDOUT, ECONNRESET, or EPIPE OR the client was killed unexpectedly - start new client connection */
        if (totalsent < 0) {
            close(connfd);
            connfd = -1;
            continue;
        }

        /* After sending the result to the client, update the pcc_total global data structure */
        for (i = 0; i < 95; i++){
            pcc_total[i] += curr_pcc_count[i];
        }

        close(connfd);
    }

    return 0;
}


/*
* A function to handle SIGINT signal with SIG_IGN
* Ignored signal behavior
* helped here - recitation3 signal_handler.c code
*/
void define_sigint() {
    struct sigaction new_action = {
        .sa_sigaction = sigint_handler,
        .sa_flags = SA_RESTART,
    };  
    
    if (sigaction(SIGINT, &new_action, NULL) == -1) {
        perror("ERROR: Failed executing SIGINT sigaction");
        exit(1);
    }
}

void sigint_handler() {
    if (client_in_process) {
        sigint = 1;
    } else {
        terminate_server();
    }
}

/*
* read from connection fd iterativly and returns the number of bytes read
*
*/
int read_data_from_client(int connfd, char* buffer, int count) {
    int bytes_read = 1;
    int totalread = 0;
    int curr_count = count;
    
    while (bytes_read > 0) {
        bytes_read = read(connfd, buffer+totalread, curr_count);
        
        if (bytes_read < 0) {
            totalread = -1;
            break;
        }

        totalread += bytes_read;
        curr_count -= bytes_read;
    }

    /* check for errors */
    if (bytes_read == 0) {
        if (totalread != count) {
            perror("ERROR: Client connections may have terminated due to client process was killed unexpectedly");
            totalread = -1;
        }
    }
    if (bytes_read < 0) {
        if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
            perror("ERROR: an error accured while reading from client, closing current client connection");
        } else {
            perror("ERROR: an error accured while reading from client, exit server");
            exit(1);
        }
    }
    return totalread;
}

/*
* write to connection fd iterativly and returns the number of bytes written
*
*/
int send_data_to_client(int connfd, char* buffer, int count) {
    int bytes_sent = 1;
    int totalsent = 0;
    int curr_count = count;

    /* iterate until all buffer bytes are written to connection fd */
    while (bytes_sent > 0) {
        bytes_sent = write(connfd, buffer+totalsent, curr_count);

        if (bytes_sent < 0) {
            totalsent = -1;
            break;
        }
        totalsent += bytes_sent;
        curr_count -= bytes_sent;
    }

    /* check for errors */
    if (bytes_sent == 0) {
        if (totalsent != count) {
            perror("ERROR: Client connections may have terminated due to client process was killed unexpectedly");
            totalsent = -1;
        }
    }
    if (bytes_sent < 0) {
        if (errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE) {
            perror("ERROR: an error accured while reading from client, closing current client connection");
        } else {
            perror("ERROR: an error accured while reading from client, exit server");
            exit(1);
        }
    }

    return totalsent;
}

/*
* For every printable character, print the number of times it was observed 
*/
void terminate_server() {
    int i = 0;

    for (i = 0; i < 95; i++){
        printf("char '%c' : %u times\n", i+32, pcc_total[i]);
    }
    exit(0);
}
