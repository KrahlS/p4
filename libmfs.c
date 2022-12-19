#include <sys/select.h>
#include <time.h>
#include <stdio.h>
#include "message.h"
#include "mfs.h"
#include "udp.h"

struct sockaddr_in addrSnd,addrRcv;
int sd;
int server_stat = 0;

int MFS_Init(char *hostname, int port){
 
    // bind to port using Piazza fix @1885
    int MIN_PORT = 20000;
    int MAX_PORT = 40000;

    srand(time(0));
    int port_num = (rand() % (MAX_PORT - MIN_PORT) + MIN_PORT);

    sd  = UDP_Open(port_num);
    int rc = UDP_FillSockAddr(&addrSnd, hostname, port);
    assert(rc>-1);
    server_stat = 1;
    
    return 0;
}

int MFS_Lookup(int pinum, char *name){

    if(pinum < 0 || strlen(name) == 0){
        return -1;
    }
    if(!server_stat){
        return -1;
    }

    message_t message;
    message.mtype = MFS_LOOKUP;
    message.inum = pinum;
    strcpy(message.name,name);

    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        return -1;
    }
    rc = UDP_Read(sd, &addrRcv, (char *)&message, sizeof(message_t));
    if(message.rc != 0){
        return -1;
    }
    return message.inum;
}

int MFS_Stat(int inum, MFS_Stat_t *m){

    if(inum < 0 || m == NULL){
        return -1;
    }

    if(!server_stat){
        return -1;
    }

    message_t message;
    message.mtype = MFS_STAT;
    message.inum = inum;

    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        return -1;
    }

    rc = UDP_Read(sd, &addrRcv, (char *)&message, sizeof(message_t));
    if(message.rc!=0) {
        return -1;
    }

    m->type = message.type;
    m->size = message.nbytes;
    fprintf(stderr,"Stat returned type %d size %d\n",m->type,m->size);

    return 0;
}

int MFS_Write(int inum, char *buffer, int offset, int nbytes){

    if(inum < 0 || strlen(buffer) == 0 || offset < 0 || nbytes > 4096){
        return -1;
    }

    message_t message;
    message.mtype = MFS_WRITE;
    message.inum = inum;
    memcpy(message.buffer,buffer,nbytes);
    message.offset = offset;
    message.nbytes = nbytes;

    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        return -1;
    }

    rc = UDP_Read(sd, &addrRcv, (char *)&message, sizeof(message_t));
    if(message.rc!=0) {
        return -1;
    }
    return 0;
}

int MFS_Read(int inum, char *buffer, int offset, int nbytes){

    if(inum < 0 || offset < 0 || nbytes < 0){
        return -1;
    }

    if(!server_stat){
        return -1;
    } 

    message_t message;
    message.mtype = MFS_READ;
    message.inum = inum;
    message.offset = offset;
    message.nbytes = nbytes;

    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        return -1;
    }

    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) {
        return -1;
    }
    memcpy(buffer,message.buffer,nbytes);

    return 0;
}

int MFS_Creat(int pinum, int type, char *name){

    if(pinum < 0 || strlen(name) < 0  || type > 1 || type < 0){
        return -1;
    }

    if(!server_stat){
        return -1;
    }

    if(strlen(name)>=28) {
        return -1;
    }

    message_t message;
    message.mtype = MFS_CRET;
    message.inum = pinum;
    message.type = type;
    strcpy(message.name,name);

    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        return -1;
    }

    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) {
        return -1;
    }

    return 0;
}

int MFS_Unlink(int pinum, char *name){

    if(pinum < 0 || strlen(name) < 0){
        return -1;
    }

    if(!server_stat){
        return -1;
    }

    message_t message;
    message.mtype = MFS_UNLINK;
    message.inum = pinum;
    strcpy(message.name,name);

    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        return -1;
    }

    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0){
        return -1;
    }

    return 0;
}

int MFS_Shutdown(){

    if(!server_stat){
        return -1;
    } 

    message_t message;
    message.mtype = MFS_SHUTDOWN;

    int rc = UDP_Write(sd, &addrSnd, (char *)(&message), sizeof(message_t));
    if(rc<0){
        return -1;
    }
    return 0;
}