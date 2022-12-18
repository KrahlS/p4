#include <sys/select.h>
#include <time.h>
#include <stdio.h>
#include "mfs.h"
#include "udp.h"
#include "message.h" // change this

struct sockaddr_in sendAddr, recAddr;
struct sockaddr_in addrSnd,addrRcv;
int sd; // socket descriptor

int MFS_Init(char *hostname, int port){
    int MIN_PORT = 20000;
    int MAX_PORT = 40000;

    srand(time(0)); //TODO: maybe delete?
    int port_num = (rand() % (MAX_PORT - MIN_PORT) + MIN_PORT);

    sd  = UDP_Open(port_num);
    int rec = UDP_FillSockAddr(&sendAddr, hostname, port);
    assert(rec>-1);
    return 0;
}

int MFS_Lookup(int pinum, char *name){
    fprintf(stderr,"Lookup pinum: %d name: %s\n",pinum,name);
    message_t message;
    message.mtype = MFS_LOOKUP;
    message.inum = pinum;
    strcpy(message.name,name);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    return message.inum;
}

int MFS_Stat(int inum, MFS_Stat_t *m){
    fprintf(stderr,"Stat inum: %d\n",inum);
    message_t message;
    message.mtype = MFS_STAT;
    message.inum = inum;
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    m->type = message.type;
    m->size = message.nbytes;
    fprintf(stderr,"Stat returned type %d size %d\n",m->type,m->size);
    return 0;
}
int MFS_Write(int inum, char *buffer, int offset, int nbytes){
    fprintf(stderr,"Write inum: %d offset: %d bytes: %d\n",inum,offset,nbytes);
    if(nbytes>4096) return -1;
    message_t message;
    message.mtype = MFS_WRITE;
    message.inum = inum;
    message.offset = offset;
    message.nbytes = nbytes;
    memcpy(message.buffer,buffer,nbytes);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    return 0;
}
int MFS_Read(int inum, char *buffer, int offset, int nbytes){
    fprintf(stderr,"Read inum: %d offset: %d bytes: %d",inum,offset,nbytes);
    message_t message;
    message.mtype = MFS_READ;
    message.inum = inum;
    message.offset = offset;
    message.nbytes = nbytes;
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    memcpy(buffer,message.buffer,nbytes);
    return 0;
}
int MFS_Creat(int pinum, int type, char *name){
    fprintf(stderr,"Create pinum: %d type: %d name: %s",pinum,type,name);
    if(strlen(name)>=28) return -1;
    message_t message;
    message.mtype = MFS_CRET;
    message.inum = pinum;
    message.type = type;
    strcpy(message.name,name);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    return 0;}
int MFS_Unlink(int pinum, char *name){
    fprintf(stderr,"Unlink pinum: %d name: %s\n",pinum,name);
    message_t message;
    message.mtype = MFS_UNLINK;
    message.inum = pinum;
    strcpy(message.name,name);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    fprintf(stderr,"client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    fprintf(stderr,"Unlink return code %d %d\n",rc, message.rc);
    if(message.rc!=0) return -1;
    return 0;}
int MFS_Shutdown(){
    message_t message;
    message.mtype = MFS_SHUTDOWN;
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    return 0;
}