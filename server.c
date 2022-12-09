#include mkfs.c
#include libmfs.so

            //send the response back to the client
            int bytes_sent = UDP_Write(sd, &s, return_buffer, BUFFER_SIZE);
            if (bytes_sent != BUFFER_SIZE) { // Check this
                fprintf(stderr, "Error: failed to send response\n");
            }
        }
    }

    return 0;
}

