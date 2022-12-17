#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BUFFER_SIZE (5000)
int fileSystemImage;
super_t* s;
char* inode_bitmap;
char* data_bitmap;
inode_t* inode_table;
void* image;
int sd;

// Signal handler for interrupt signal (Ctrl + C)
void intHandler(int dummy) {
    UDP_Close(sd);
    exit(130);
}

// Returns the value of the bit at the specified position in the bitmap
unsigned int get_bit(unsigned int *bitmap, int position) {
    return (bitmap[position / 32] >> (31 - (position % 32))) & 0x1;
}

// Sets the value of the bit at the specified position in the bitmap to 1
void set_bit(unsigned int *bitmap, int position) {
    bitmap[position / 32] |= 0x1 << (31 - (position % 32));
}

// Sets the value of the bit at the specified position in the bitmap to 0
void clear_bit(unsigned int *bitmap, int position){
    bitmap[position / 32] &= ~(0x1 << (31 - (position % 32)));
}

// Returns a pointer to the inode with the specified inode number, or null if it does not exist
inode_t* get_inode(int inum){
    // Check if inode is out of range
    if(inum>=s->num_inodes){
        fprintf(stderr,"inum out of range\n");
        return 0;
    }
    // Check if inode is marked as allocated in the inode bitmap
    if(get_bit((unsigned int*)inode_bitmap,inum)==0) return 0;
    return &(inode_table[inum]);
}

// Returns a pointer to the specified offset in the specified inode
char* get_pointer(inode_t* inode, int offset){
    return (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE+offset%UFS_BLOCK_SIZE;
}

// Finds the first free byte in the specified bitmap and sets it to used
// Returns the position of the free byte, or -1 if no free bytes are found
int find_free(char* bitmap, int length){
    for(int i = 0; i<length; i++){
        if(get_bit((unsigned int*) bitmap, i)==0){
            set_bit((unsigned int*) bitmap, i);
            return i;
        }
    }
    return -1;
}

// Reads data from the specified inode at the specified offset
// Returns 0 on success, or -1 on failure
int file_read(int inum, char *buffer, int offset, int nbytes) {
    // Get the inode with the specified inode number
    inode_t* inode = get_inode(inum);
    if (!inode) {
        // Inode does not exist
        return -1;
    }

    // Check if the read is within the bounds of the file
    if (offset + nbytes > inode->size) {
        fprintf(stderr, "Read too big\n");
        return -1;
    }

    // Check if the read spans across one or two blocks
    if (offset % UFS_BLOCK_SIZE + nbytes <= UFS_BLOCK_SIZE) {
        // One block
        char* block = (char*)image + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        memcpy((void*)buffer, (void*)(block + offset % UFS_BLOCK_SIZE), nbytes);
    } else {
        // Two blocks
        char* block1 = (char*)image + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        char* block2 = (char*)image + (inode->direct[offset / UFS_BLOCK_SIZE + 1]) * UFS_BLOCK_SIZE;
        memcpy((void*)buffer, (void*)(block1 + offset % UFS_BLOCK_SIZE), UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE);
        memcpy((void*)(buffer + UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE), (void*)(block2), nbytes - (UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE));
    }

    return 0;
}

// Writes data to the specified inode at the specified offset
// Returns 0 on success, or -1 on failure
int file_write(int inum, char *buffer, int offset, int nbytes) {
    // Get the inode with the specified inode number
    inode_t* inode = get_inode(inum);
    if (!inode) {
        // Inode does not exist
        return -1;
    }

    // Check if the inode is a directory
    if (inode->type == UFS_DIRECTORY) {
        return -1;
    }

    // Check if the write is too large
    if (nbytes > 4096) {
        return -1;
    }

    // Check if the write is within the bounds of the file
    if (offset + nbytes > DIRECT_PTRS * UFS_BLOCK_SIZE) {
        return -1;
    }
        // Check if a new block needs to be allocated
    if ((offset + nbytes) / UFS_BLOCK_SIZE > inode->size / UFS_BLOCK_SIZE) {
        // Allocate a new data block
        int data_block = find_free(data_bitmap, s->data_region_len);
        if (data_block < 0) {
            // No free data blocks available
            return -1;
        }
        inode->direct[inode->size / UFS_BLOCK_SIZE + 1] = data_block + s->data_region_addr;
        inode->size = offset + nbytes;
    } else {
        // No need to allocate a new block
        inode->size = MAX(inode->size, offset + nbytes);
    }

    // Check if the write spans across one or two blocks
    if (offset % UFS_BLOCK_SIZE + nbytes <= UFS_BLOCK_SIZE) {
        // One block
        char* block = (char*)image + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        memcpy((void*)(block + offset % UFS_BLOCK_SIZE), (void*)buffer, nbytes);
    } else {
        // Two blocks
        char* block1 = (char*)image + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        char* block2 = (char*)image + (inode->direct[offset / UFS_BLOCK_SIZE + 1]) * UFS_BLOCK_SIZE;
        memcpy((void*)(block1 + offset % UFS_BLOCK_SIZE), (void*)buffer, UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE);
        memcpy((void*)(block2), (void*)(buffer + UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE), nbytes - (UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE));
    }

    fprintf(stderr, "Wrote %d bytes at %d offset, new file size is now %d\n", nbytes, offset, inode->size);
    return 0;
}

int main(int argc, char *argv[]) {
    return 0; 
}