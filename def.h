/*
    Defines some constant and header information.
    Your code should reference those defined constants instead of
    directly writing numbers in your code.
*/

#ifndef DEF_HEADER
#define DEF_HEADER

#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

// segment data size
#define MAX_SEG_SIZE 1000

// receiver buffer size
#define MAX_SEG_BUF_SIZE 256

// sender transmit queue max size, receiver app data max size
#define MAX_SEG_QUE_SIZE 2500

// the timeout is set to `(TIMEOUT_MILLISECONDS) msec` for sender
#define TIMEOUT_MILLISECONDS 1000

// max file content buffer size
#define BUF_SIZE 2621440    // 2.5 MB

// header definition
struct header {
    int length;             // number of bytes of the data. does not contain header!
    int seqNumber;          // sender: current segment's sequence number, start at 1
    int ackNumber;          // receiver: cumulative ack number, 
                            //           the seq_num of last cumulative acked packets
    int sackNumber;         // receiver: selective ack number
    int fin;                // 1 if this is the last packet else 0
                            // if this is a fin (not finack) packet, seqNumber should
                            // be the last segment's seqNumber + 1
    int syn;                // (just make it 0 for this assignment)
    int ack;                // 1 if this is an ack packet else 0
    unsigned int checksum;  // sender: crc32 checksum of data
    int sent;               // sender: 1 if it is sent before
};

typedef struct segment {
    struct header head;
    char data[MAX_SEG_SIZE];
} segment;

// state definition
typedef enum {
    SLOW_START, CONGEST_AVOID
} State;

#endif // DEF_HEADER