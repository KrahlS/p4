#ifndef __message_h__
#define __message_h__

#define MFS_INIT (1)
#define MFS_LOOKUP (2)
#define MFS_STAT (3)
#define MFS_WRITE (4)
#define MFS_READ (5)
#define MFS_CRET (6)
#define MFS_UNLINK (7)
#define MFS_SHUTDOWN (8)


typedef struct {
    int mtype; // message type from above
    int rc;    // return code
    char buffer[4096];
    char name[28];
    int offset;
    int nbytes;
    int type;
    int inum;
} message_t;

#endif // __message_h__
