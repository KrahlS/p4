#include <stdio.h>
#include <unistd.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>

inode_t* inode_table;
void* img;
int sd;
int fs_img;
super_t* s;
char* inode_bitmap;
char* data_bitmap;
#define BUFFER_SZ (5000)

/*
* HELPER FUNCTIONS: 
*   used for bit manipulation & fetching pointers, bytes, and inodes 
*/

// Sets the value of the bit at the specified position in the bitmap to 1
void bit_set(unsigned int *bitmap, int position) {
    bitmap[position / 32] |= 0x1 << (31 - (position % 32));
}

// Returns the value of the bit at the specified position in the bitmap
unsigned int bit_fetch(unsigned int *bitmap, int position) {
    return (bitmap[position / 32] >> (31 - (position % 32))) & 0x1;
}

// Sets the value of the bit at the specified position in the bitmap to 0
void bit_clear(unsigned int *bitmap, int position){
    bitmap[position / 32] &= ~(0x1 << (31 - (position % 32)));
}

// Returns a pointer to the specified offset in the specified inode
char* fetch_ptr(inode_t* inode, int offset){
    return (char*)img+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE+offset%UFS_BLOCK_SIZE;
}

// Finds the first free byte in the specified bitmap and sets it to used
// Returns the position of the free byte, or -1 if no free bytes are found
int locate_free_byte(char* bitmap, int length){
    for(int i = 0; i<length; i++){
        if(bit_fetch((unsigned int*) bitmap, i)==0){
            bit_set((unsigned int*) bitmap, i);
            return i;
        }
    }
    return -1;
}

// Returns a pointer to the inode with the specified inode number, or null if it does not exist
inode_t* fetch_inode(int inum){
    // Check if inode is out of range
    if(inum>=s->num_inodes){
        return 0;
    }
    // Check if inode is marked as allocated in the inode bitmap
    if(bit_fetch((unsigned int*)inode_bitmap,inum)==0) return 0;
    return &(inode_table[inum]);
}

// Signal handler for interrupt signal (Ctrl + C)
void interrupt_handler(int dummy) {
    UDP_Close(sd);
    exit(130);
}

/**
 * This function creates a new file or directory in the file system.
 *
 *  pinum:  the inode number of the parent directory
 *  type:   the type of file to fs_create (UFS_REGULAR or UFS_DIRECTORY)
 *  name:   the name of the file or directory to fs_create
 *
 *  returns:  an integer indicating the success or failure of the operation
 *            (1 for success, -1 for failure)
 */
int fs_create(int pinum, int type, char *name){
    // Validate inode of the parent 
    inode_t* pinode = fetch_inode(pinum);
    if(pinode==0 || pinode->type!=UFS_DIRECTORY || strlen(name)>=28) 
        return -1;

    // Check for existing directories 
    dir_ent_t* dir;
    int i; 
    while((i<pinode->size/sizeof(dir_ent_t)) && 
        (dir = (dir_ent_t*) fetch_ptr(pinode, i*sizeof(dir_ent_t)))->inum != -1) {
            if(strcmp(dir->name,name)==0){
                return 0;
            }
            i++;
    }

    int empty = 0; // empty flag indicator
    // Find first empty directory entry
    i = 0; 
    while ((i < pinode->size/sizeof(dir_ent_t)) && 
        (dir = (dir_ent_t*) fetch_ptr(pinode, i * sizeof(dir_ent_t)))->inum != -1) {
        // If directory entry is empty, add new directory
        if (dir->inum == -1) {
            strcpy(dir->name, name);

            // Allocate new inode for new directory
            int index = locate_free_byte(inode_bitmap, s->num_inodes);
            if (index < 0) return -1;
            dir->inum = index;
            inode_t* inode = &inode_table[index];

            // Allocate new data block for new directory
            int data_block = locate_free_byte(data_bitmap, s->data_region_len);
            if (data_block < 0) return -1;
            inode->direct[0] = data_block + s->data_region_addr;
            inode->size = 0;
            inode->type = type;

            // Add "." and ".." entries to new directory
            if (inode->type == UFS_DIRECTORY) {
            dir_ent_t* self = (dir_ent_t*)fetch_ptr(inode, 0);
            sprintf(self->name, ".");
            self->inum = index;
            dir_ent_t* parent = (dir_ent_t*)fetch_ptr(inode, sizeof(dir_ent_t));
            sprintf(parent->name, "..");
            parent->inum = pinum;
            inode->size = 2 * sizeof(dir_ent_t);
            }
            empty = 1;
        }
        i++;
    }

    // Intermediate empty slot not found, push onto end 
    if (!empty) {
        // Allocate new block for parent directory if full
        if (pinode->size % UFS_BLOCK_SIZE == 0) {
            int data_block = locate_free_byte(data_bitmap, s->data_region_len);
            if (data_block < 0) return -1;
            pinode->direct[pinode->size / UFS_BLOCK_SIZE] = data_block + s->data_region_addr;
        }

        // Push new directory entry onto end of parent directory
        pinode->size += sizeof(dir_ent_t);
        dir_ent_t* dir = (dir_ent_t*) fetch_ptr(pinode, pinode->size - sizeof(dir_ent_t));
        strcpy(dir->name, name);

        // Allocate new inode for new directory
        int index = locate_free_byte(inode_bitmap, s->num_inodes);

        if (index < 0) 
            return -1;

        dir->inum = index;
        inode_t* inode = &inode_table[index];

        // Allocate new data block for new directory
        int data_block = locate_free_byte(data_bitmap, s->data_region_len);

        if (data_block < 0) 
            return -1;

        inode->direct[0] = data_block + s->data_region_addr;
        inode->size = 0;
        inode->type = type;

        // Add "." and ".." entries to new directory
        if (inode->type == UFS_DIRECTORY) {
            dir_ent_t* self = (dir_ent_t*)fetch_ptr(inode, 0);
            sprintf(self->name, ".");
            self->inum = index;
            dir_ent_t* parent = (dir_ent_t*)fetch_ptr(inode, sizeof(dir_ent_t));
            sprintf(parent->name, "..");
            parent->inum = pinum;
            inode->size = 2 * sizeof(dir_ent_t);
        }

    }

    return 0;
}

/**
 * This function writes data to a file in the file system.
 *
 *  inum:    the inode number of the file to write to
 *  buffer:  the buffer containing the data to write
 *  offset:  the offset in the file to start writing from
 *  nbytes:  the number of bytes to write
 *
 *  returns: an integer indicating the success or failure of the operation
 *           (0 for success, -1 for failure)
 */

int fs_write(int inum, char *buffer, int offset, int nbytes) {
    // Get the inode with the specified inode number
    inode_t* inode = fetch_inode(inum);
    if (!inode || (inode->type == UFS_DIRECTORY) || (nbytes > 4096)) {
        // Inode does not exist
        return -1;
    }

    // Check if the write is within the bounds of the file
    if (offset + nbytes > DIRECT_PTRS * UFS_BLOCK_SIZE) {
        return -1;
    }
        // Check if a new block needs to be allocated
    if ((offset + nbytes) / UFS_BLOCK_SIZE > inode->size / UFS_BLOCK_SIZE) {
        // Allocate a new data block
        int data_block = locate_free_byte(data_bitmap, s->data_region_len);
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
        char* block = (char*)img + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        memcpy((void*)(block + offset % UFS_BLOCK_SIZE), (void*)buffer, nbytes);
    } else {
        // Two blocks
        char* block1 = (char*)img + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        char* block2 = (char*)img + (inode->direct[offset / UFS_BLOCK_SIZE + 1]) * UFS_BLOCK_SIZE;
        memcpy((void*)(block1 + offset % UFS_BLOCK_SIZE), (void*)buffer, UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE);
        memcpy((void*)(block2), (void*)(buffer + UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE), nbytes - (UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE));
    }
    return 0;
}

/**
 * This function reads data from a file in the file system.
 *
 *  inum:    the inode number of the file to read from
 *  buffer:  the buffer to store the read data
 *  offset:  the offset in the file to start reading from
 *  nbytes:  the number of bytes to read
 *
 *  returns: an integer indicating the success or failure of the operation
 *           (0 for success, -1 for failure)
 */

int fs_read(int inum, char *buffer, int offset, int nbytes) {
    // Get the inode with the specified inode number
    inode_t* inode = fetch_inode(inum);
    if (!inode) {
        // Inode does not exist
        return -1;
    }

    // Check if the read is within the bounds of the file
    if (offset + nbytes > inode->size) {
        return -1;
    }

    // Check if the read spans across one or two blocks
    if (offset % UFS_BLOCK_SIZE + nbytes <= UFS_BLOCK_SIZE) {
        // One block
        char* block = (char*)img + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        memcpy((void*)buffer, (void*)(block + offset % UFS_BLOCK_SIZE), nbytes);
    } else {
        // Two blocks
        char* block1 = (char*)img + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        char* block2 = (char*)img + (inode->direct[offset / UFS_BLOCK_SIZE + 1]) * UFS_BLOCK_SIZE;
        memcpy((void*)buffer, (void*)(block1 + offset % UFS_BLOCK_SIZE), UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE);
        memcpy((void*)(buffer + UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE), (void*)(block2), nbytes - (UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE));
    }

    return 0;
}


/*
Returns an integer representing the type and size of a file within a distributed file system built on a UDP connection.

Arguments:
    inode_num: an integer representing the inode number of the file to be queried.

Returns:
    The type and size of the file encoded in an integer if the inode is found.
    -1 if the inode is not found.
*/
int fs_stat(int inode_num) {
    // Get the inode for the file and return -1 if it is not found
    inode_t* inode = fetch_inode(inode_num);
    return inode ? (inode->size << 1) + inode->type : -1;
}

/*
Looks up a file within a distributed file system built on a UDP connection.

Arguments:
    pinum: an integer representing the inode number of the parent directory of the file to be looked up.
    name: a string representing the name of the file to be looked up.

Returns:
    The inode number of the file if it is found in the parent directory.
    -1 if the parent inode is not found or is not a directory, or if the file is not found in the parent directory.
*/
int fs_lookup(int pinum, char *name) {
    // Get the inode for the parent directory and return -1 if it is not found or is not a directory
    inode_t* pinode = fetch_inode(pinum);
    if(pinode == 0 || pinode->type != UFS_DIRECTORY) return -1;

    // Iterate through the directory entries in the parent inode
    int i = 0; 
    while (i < pinode->size / sizeof(dir_ent_t)){
        // Get the current directory entry
        dir_ent_t* dir = (dir_ent_t*) fetch_ptr(pinode, i * sizeof(dir_ent_t));

        // Check if the current directory entry is the file to be looked up
        if(dir->inum != -1 && strcmp(dir->name, name) == 0) {
            // Return the inode number of the file if it is found
            return dir->inum;
        }
        i++; 
    }

    // Return -1 if the file is not found in the parent directory
    return -1;
}

/*
Unlinks (deletes) a file within a distributed file system built on a UDP connection.

Arguments:
    pinum: an integer representing the inode number of the parent directory of the file to be unlinked.
    name: a string representing the name of the file to be unlinked.

Returns:
    0 if the file was successfully unlinked.
    -1 if the parent inode is not found or is not a directory, or if the file to be unlinked is a non-empty directory.
    0 if the file was not found in the parent directory.
*/
int fs_unlink(int pinum, char *name) {
    // Get the inode for the parent directory and return -1 if it is not found
    inode_t* pinode = fetch_inode(pinum);
    if(pinode == 0 || pinode->type != UFS_DIRECTORY) return -1;

    // Iterate through the directory entries in the parent inode until the file is found
    int i = 0;
    while(i < pinode->size / sizeof(dir_ent_t)) {
        // Get the current directory entry
        dir_ent_t* dir = (dir_ent_t*) fetch_ptr(pinode, i * sizeof(dir_ent_t));
        // Check if the current directory entry is the file to be unlinked
        if(dir->inum != -1 && strcmp(dir->name, name) == 0) {
            // Get the inode for the file to be unlinked and return -1 if it is a non-empty directory
            inode_t* inode = fetch_inode(dir->inum);
            if(inode->type == UFS_DIRECTORY) {
                int j = 2;
                while(j < inode->size / sizeof(dir_ent_t)) {
                    dir_ent_t* entry = (dir_ent_t*) fetch_ptr(inode, j * sizeof(dir_ent_t));
                    if(entry->inum != -1) return -1;
                    j++;
                }
            }

            // Unlink the file by setting its inum to -1 in the directory entry
            dir->inum = -1;

            // Clear the data blocks used by the file from the data bitmap
            for(int j = 0; j <= inode->size / UFS_BLOCK_SIZE; j++) {
                bit_clear((unsigned int*)data_bitmap, inode->direct[j] - s->data_region_addr);
            }
            // Clear the inode from the inode bitmap
            bit_clear((unsigned int*)inode_bitmap, dir->inum);
            return 0;
        }
        i++;
    }

    // Return 0 if the file was not found in the parent directory
    return 0;
}


int main(int argc, char *argv[]) {
  signal(SIGINT, interrupt_handler);

  // Check number of arguments
  if (argc != 3) {
    return 1;
  }

  // Open socket and file system img
  int portnum = atoi(argv[1]);
  sd = UDP_Open(portnum);
  if (sd < 0) {
    return 1;
  }
  fs_img = open(argv[2], O_RDWR|O_SYNC);
  if (fs_img == -1) {
    return -1;
  }

  // Map file system img to memory
  struct stat sbuf;
  int rc = fstat(fs_img, &sbuf);
  if (rc < 0) {
    return 1;
  }
  img = mmap(NULL, sbuf.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fs_img, 0);
  if (img == MAP_FAILED) {
    return 1;
  }

  // Get superblock, inode table, and bitmaps
  s = (super_t *)img;
  inode_table = img + (s->inode_region_addr * UFS_BLOCK_SIZE);
  inode_bitmap = img + (s->inode_bitmap_addr * UFS_BLOCK_SIZE);
  data_bitmap = img + (s->data_bitmap_addr * UFS_BLOCK_SIZE);

  // Main loop
  while (1) {
    struct sockaddr_in addr;
    int result;
    message_t* message = malloc(sizeof(message_t));
    int rc = UDP_Read(sd, &addr, (char*)message, BUFFER_SZ);
    if (rc <= 0) continue;

    // Handle message based on type
    switch(message->mtype) {
      case MFS_INIT:
        break;
      case MFS_STAT:
        result = fs_stat(message->inum);
        if (result == -1) {
          message->rc = -1;
        } else {
          message->type = result & 1;
          message->nbytes = result / 2;
        }
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        break;
      case MFS_LOOKUP:
        result = fs_lookup(message->inum, message->name);
        message->inum = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        break;
      case MFS_CRET:
        result = fs_create(message->inum, message->type, message->name);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        break;
      case MFS_WRITE:
        result = fs_write(message->inum, message->buffer, message->offset, message->nbytes);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        break;
      case MFS_READ:
        result = fs_read(message->inum, message->buffer, message->offset, message->nbytes);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        break;
      case MFS_UNLINK:
        result = fs_unlink(message->inum, message->name);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        break;
    case MFS_SHUTDOWN:
        exit(0);
        break;
    default:
        fprintf(stderr, "Invalid Request\n");
        break;
    }
    free(message);
  }
  return 0;
}
     