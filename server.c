#include mkfs.c
#include <stdio.h>
#include "udp.h"

#define BUFFER_SIZE (1000)

// server code
int main(int argc, char *argv[]) {
    // open UDP port
    char *a = argv[1]
    int port_num = atoi(a);
    int sd = UDP_Open(port_num);
    assert(sd > -1);

    while (1) {
	    struct sockaddr_in addr;
	    char message[BUFFER_SIZE];
	    printf("server:: waiting...\n");
	    int rc = UDP_Read(sd, &addr, message, BUFFER_SIZE);
	    printf("server:: read message [size:%d contents:(%s)]\n", rc, message);
	    if (rc > 0) {
                char reply[BUFFER_SIZE];
                sprintf(reply, "goodbye world");
                rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
	        printf("server:: reply\n");
	    } 
    }
    return 0; 
}
    
// 
            // send the response back to the client
            // int bytes_sent = UDP_Write(sd, &s, return_buffer, BUFFER_SIZE);
            // if (bytes_sent != BUFFER_SIZE) { // Check this
                // fprintf(stderr, "Error: failed to send response\n");
            // }
        // }
    // }
// 
    // return 0;
// }

