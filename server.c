#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"

inode_t* inode_table;
void* image;
int sd;
int fileSystemImage;
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
    return (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE+offset%UFS_BLOCK_SIZE;
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
        fprintf(stderr,"inum out of range\n");
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
    inode_t* parent_inode = fetch_inode(pinum);
    if(parent_inode==0) return -1;
    if(parent_inode->type!=UFS_DIRECTORY) return -1;
    if(strlen(name)>=28) return -1;

    //Check for same name
    for(int i = 0; i<parent_inode->size/sizeof(dir_ent_t); i++){
        dir_ent_t* directory_entry = (dir_ent_t*) fetch_ptr(parent_inode,i*sizeof(dir_ent_t));
        if(directory_entry->inum!=-1 && strcmp(directory_entry->name,name)==0){
            return 0;
        }
    }
    int flag = 0;
    //Find an open slot in the parent directory
    for(int i = 0; i<parent_inode->size/sizeof(dir_ent_t); i++){
        dir_ent_t* directory_entry = (dir_ent_t*) fetch_ptr(parent_inode,i*sizeof(dir_ent_t));
        if(directory_entry->inum==-1){
            strcpy(directory_entry->name,name);
            int index = locate_free_byte(inode_bitmap,s->num_inodes);
            if(index<0) return -1;
            directory_entry->inum = index;
            inode_t* inode = &inode_table[index];
            int data_block = locate_free_byte(data_bitmap,s->data_region_len);
            if(data_block<0) return -1;
            inode->direct[0] = data_block+s->data_region_addr;
            inode->size = 0;
            inode->type = type;
            if(inode->type==UFS_DIRECTORY){
                dir_ent_t* self = (dir_ent_t*)fetch_ptr(inode,0);
                sprintf(self->name,".");
                self-> inum = index;
                dir_ent_t* parent = (dir_ent_t*)fetch_ptr(inode, sizeof(dir_ent_t));
                sprintf(parent->name,"..");
                parent -> inum = pinum;
                inode->size = 2*sizeof(dir_ent_t);
            }
            flag = 1;
        }
    }

    //Didn't find empty slot within, so add onto the end
    if(flag!=1){   
        //If parent directory is full allocate a new block for it
        if(parent_inode->size%UFS_BLOCK_SIZE==0){
            int data_block = locate_free_byte(data_bitmap,s->data_region_len);
            if(data_block<0) return -1;
            parent_inode->direct[parent_inode->size/UFS_BLOCK_SIZE] = data_block+s->data_region_addr;
        }
        parent_inode->size +=sizeof(dir_ent_t);
        //Get last directory entry
        dir_ent_t* directory_entry = (dir_ent_t*) fetch_ptr(parent_inode,parent_inode->size-sizeof(dir_ent_t));
        strcpy(directory_entry->name,name);
        int index = locate_free_byte(inode_bitmap,s->num_inodes);
        if(index<0) return -1;
        directory_entry->inum = index;
        inode_t* inode = &inode_table[index];
        int data_block = locate_free_byte(data_bitmap,s->data_region_len);
        if(data_block<0) return -1;
        inode->direct[0] = data_block+s->data_region_addr;
        inode->size = 0;
        inode->type = type;
        if(inode->type == UFS_DIRECTORY){
            dir_ent_t* self = (dir_ent_t*)fetch_ptr(inode,0);
            sprintf(self->name,".");
            self-> inum = index;
            dir_ent_t* parent = (dir_ent_t*)fetch_ptr(inode, sizeof(dir_ent_t));
            sprintf(parent->name,"..");
            parent -> inum = pinum;
            inode->size = 2*sizeof(dir_ent_t);
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
    inode_t* parent_inode = fetch_inode(pinum);
    if(parent_inode == 0 || parent_inode->type != UFS_DIRECTORY) return -1;

    // Iterate through the directory entries in the parent inode
    int i = 0; 
    while (i < parent_inode->size / sizeof(dir_ent_t)){
        // Get the current directory entry
        dir_ent_t* directory_entry = (dir_ent_t*) fetch_ptr(parent_inode, i * sizeof(dir_ent_t));

        // Check if the current directory entry is the file to be looked up
        if(directory_entry->inum != -1 && strcmp(directory_entry->name, name) == 0) {
            // Return the inode number of the file if it is found
            return directory_entry->inum;
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
    inode_t* parent_inode = fetch_inode(pinum);
    if(parent_inode == 0 || parent_inode->type != UFS_DIRECTORY) return -1;

    // Iterate through the directory entries in the parent inode until the file is found
    int i = 0;
    while(i < parent_inode->size / sizeof(dir_ent_t)) {
        // Get the current directory entry
        dir_ent_t* directory_entry = (dir_ent_t*) fetch_ptr(parent_inode, i * sizeof(dir_ent_t));
        // Check if the current directory entry is the file to be unlinked
        if(directory_entry->inum != -1 && strcmp(directory_entry->name, name) == 0) {
            // Get the inode for the file to be unlinked and return -1 if it is a non-empty directory
            inode_t* inode = fetch_inode(directory_entry->inum);
            if(inode->type == UFS_DIRECTORY) {
                int j = 2;
                while(j < inode->size / sizeof(dir_ent_t)) {
                    dir_ent_t* entry = (dir_ent_t*) fetch_ptr(inode, j * sizeof(dir_ent_t));
                    if(entry->inum != -1) return -1;
                    j++;
                }
            }

            // Unlink the file by setting its inum to -1 in the directory entry
            directory_entry->inum = -1;

            // Clear the data blocks used by the file from the data bitmap
            for(int j = 0; j <= inode->size / UFS_BLOCK_SIZE; j++) {
                bit_clear((unsigned int*)data_bitmap, inode->direct[j] - s->data_region_addr);
            }
            // Clear the inode from the inode bitmap
            bit_clear((unsigned int*)inode_bitmap, directory_entry->inum);
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
    fprintf(stderr, "Invalid number of arguments\n");
    return 1;
  }

  // Open socket and file system image
  int portnum = atoi(argv[1]);
  sd = UDP_Open(portnum);
  if (sd < 0) {
    fprintf(stderr, "Failed to open socket\n");
    return 1;
  }
  fileSystemImage = open(argv[2], O_RDWR|O_SYNC);
  if (fileSystemImage == -1) {
    fprintf(stderr, "Image does not exist\n");
    return -1;
  }

  // Map file system image to memory
  struct stat sbuf;
  int rc = fstat(fileSystemImage, &sbuf);
  if (rc < 0) {
    fprintf(stderr, "Failed to get file system image stat\n");
    return 1;
  }
  image = mmap(NULL, sbuf.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fileSystemImage, 0);
  if (image == MAP_FAILED) {
    fprintf(stderr, "Failed to map file system image to memory\n");
    return 1;
  }

  // Get superblock, inode table, and bitmaps
  s = (super_t *)image;
  inode_table = image + (s->inode_region_addr * UFS_BLOCK_SIZE);
  inode_bitmap = image + (s->inode_bitmap_addr * UFS_BLOCK_SIZE);
  data_bitmap = image + (s->data_bitmap_addr * UFS_BLOCK_SIZE);

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
      case MFS_LOOKUP:
        result = fs_lookup(message->inum, message->name);
        message->inum = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        fprintf(stderr, "Server: Lookup Reply\n");
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
        fprintf(stderr, "Server: Stat Reply %d %d\n", message->type, message->nbytes);
        break;
      case MFS_WRITE:
        result = fs_write(message->inum, message->buffer, message->offset, message->nbytes);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        fprintf(stderr, "Server: Write Reply\n");
        break;
      case MFS_READ:
        result = fs_read(message->inum, message->buffer, message->offset, message->nbytes);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        fprintf(stderr, "Server: Read Reply\n");
        break;
      case MFS_CRET:
        result = fs_create(message->inum, message->type, message->name);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        fprintf(stderr, "Server: Create Reply\n");
        break;
      case MFS_UNLINK:
        result = fs_unlink(message->inum, message->name);
        message->rc = result;
        rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SZ);
        fprintf(stderr, "Server: Unlink Reply\n");
        break;
    case MFS_SHUTDOWN:
        exit(0);
        break;
    default:
        fprintf(stderr, "Server: Invalid request\n");
        break;
    }
    free(message);
  }
  return 0;
}
     