#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

//connect with own files
#include "udp.h"

#define BUFFER_SIZE 4096

//function prototypes
int process_connection(char input[], char* output, int sd, struct sockaddr_in s);

int main(int argc, char *argv[])
{
    // check program arguments (port num, fs img)
    if (argc != 3) {
        fprintf(stderr, "Usage: server [portnum] [file-system-image]\n");
        exit(1);
    }

    // parse program arguments
    int port = atoi(argv[1]);
    char* file_name = argv[2];

    // TODO try initializing the file system


    // open the specified port
    printf("Opening port %d\n", port);
    int sd = UDP_Open(port);
    if (sd < 0) {
        fprintf(stderr, "Error: failed to open port %d\n", port);
        exit(1);
    }

    // main server loop
    while (true) {
        struct sockaddr_in s;
        char buffer[BUFFER_SIZE];
        char return_buffer[BUFFER_SIZE];
        int bytes_read = UDP_Read(sd, &s, buffer, BUFFER_SIZE);

        // process the received data if the read was successful
        if (bytes_read == BUFFER_SIZE) {
            // TODO add function that parses data and processes using fs methods 
            int status = process_connection(buffer, return_buffer, sd, s);

            // skip any invalid commands
            if (status == -1) {
                continue;
            }

            //send the response back to the client
            int bytes_sent = UDP_Write(sd, &s, return_buffer, BUFFER_SIZE);
            if (bytes_sent != BUFFER_SIZE) { // Check this
                fprintf(stderr, "Error: failed to send response\n");
            }
        }
    }

    return 0;
}

