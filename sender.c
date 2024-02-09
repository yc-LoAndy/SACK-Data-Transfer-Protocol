#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>        // access()
#include <fcntl.h>         // open()
#include <zlib.h>          // crc32()
#include <time.h>          // for timer
#include <sys/time.h>      // for select() related stuff
#include <math.h>          // ceil()

#include "def.h"


// Global Variables
double cwnd;
int thresh, dup_ack;
State state;
clock_t tick, tock;
segment Queue[MAX_SEG_QUE_SIZE];     // transmit queue
// Queue[0] is a dummy


// Functions
void setIP(char *dst, char *src);
double get_remaining_time();
int pack_file_data(char *filepath, uint8_t *buffer);
void create_segments(uint8_t *buffer, int total_seg, int fsize);
void initialize(int sock_fd, struct sockaddr_in *recv_addr);
void transmit_new(int sock_fd, struct sockaddr_in *recv_addr, int old_seqs[], int old_cwnd);
void transmit_missing(int sock_fd, struct sockaddr_in *recv_addr);
void reset_timer();
void set_state(int flag);
void mark_sack(int sack_num);
void update_base(int ack_num);
void timeout(int sock_fd, struct sockaddr_in *recv_addr);

// FSM callback events
void receive_ack(int sock_fd, struct sockaddr_in *recv_addr);
void recv_dup_ack(int sock_fd, struct sockaddr_in *recv_addr, segment sgmt);
void recv_new_ack(int sock_fd, struct sockaddr_in *recv_addr, segment sgmt);
int send_fin(int sock_fd, struct sockaddr_in *recv_addr);


// ./sender <send_ip> <send_port> <agent_ip> <agent_port> <src_filepath>
int main(int argc, char *argv[]) {
    // parse arguments
    if (argc != 6) {
        fprintf(stderr, "Usage: ./sender <send_ip> <send_port> <agent_ip> <agent_port> <src_filepath>\n");
        exit(1);
    }

    int send_port, agent_port;
    char send_ip[50], agent_ip[50];

    // read argument
    setIP(send_ip, argv[1]);
    sscanf(argv[2], "%d", &send_port);

    setIP(agent_ip, argv[3]);
    sscanf(argv[4], "%d", &agent_port);

    char *filepath = argv[5];

    // make socket related stuff
    int sock_fd = socket(PF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in recv_addr;
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(agent_port);
    recv_addr.sin_addr.s_addr = inet_addr(agent_ip);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(send_port);
    addr.sin_addr.s_addr = inet_addr(send_ip);
    memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

    int activity;
    fd_set readfds;

    bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));

    // Pack the file and get its size
    int fsize, total_seg;
    uint8_t *buffer = (uint8_t *) malloc(BUF_SIZE);
    fsize = pack_file_data(filepath, buffer);
    total_seg = (int) ceil( ((double)fsize) / MAX_SEG_SIZE );

    if (fsize == -1) {
        free(buffer); close(sock_fd);
        fprintf(stderr, "File doesn't exist.\n");
        exit(1);
    }

    // Initialization
    int base = 1, res = 0;
    struct timeval tv = {.tv_sec=0, .tv_usec=TIMEOUT_MILLISECONDS*1000};
    create_segments(buffer, total_seg, fsize);
    initialize(sock_fd, &recv_addr);
    free(buffer);

    // Main Loop
    while (1) {
        // printf("\nNew loop-->\n");
        // printf("Initial Queue:\n");
        // for (int i = 0; i < 6; i++) printf("%d ", Queue[i].head.seqNumber);
        // printf("\n");

        // Clear the socket set and add the master socket
        FD_ZERO(&readfds);
        FD_SET(sock_fd, &readfds);

        // Update base and check if all segments are sent and acked
        base = Queue[1].head.seqNumber;
        if (base == total_seg + 1) {
            if ((res = send_fin(sock_fd, &recv_addr)) != 0)
                fprintf(stderr, "No finack received.\n");
            break;
        }

        // Wait for activity until timeout
        tv.tv_usec = get_remaining_time()*1000;
        activity = select(sock_fd+1, &readfds, NULL, NULL, &tv);
        if (activity == 0) {
            // Timeout happens
            timeout(sock_fd, &recv_addr);
            continue;
        }

        // Receive ack segments
        receive_ack(sock_fd, &recv_addr);
    }

    close(sock_fd);
    return 0;
}

// Functions
void setIP(char *dst, char *src){
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0 || strcmp(src, "localhost") == 0){
        sscanf("127.0.0.1", "%s", dst);
    }
    else{
        sscanf(src, "%s", dst);
    }
    return;
}

double get_remaining_time() {
    /* Get the reamaining time before timeout */
    tock = clock();
    double timer = ((double)(tock - tick) / CLOCKS_PER_SEC) * 1000;
    return TIMEOUT_MILLISECONDS - timer;
}

int pack_file_data(char *filepath, uint8_t *buffer) {
    /*
        Pack the file data into the buffer.
        Return the file size in byte, or -1 if the file doesn't exist.
    */
    if (access(filepath, F_OK) != 0)
        return -1;

    // Read the file to the buffer
    int fd;
    ssize_t byt;
    fd = open(filepath, O_RDONLY);
    byt = read(fd, (void *)buffer, BUF_SIZE);
    close(fd);
    
    return (int)byt;
}

void create_segments(uint8_t *buffer, int total_seg, int fsize) {
    /* Create the transmit queue */
    memset(Queue, 0, MAX_SEG_QUE_SIZE*sizeof(segment));

    segment sgmt = { };
    Queue[0] = sgmt;    // dummy segment

    uint8_t *data_ptr = buffer;
    for (int i = 1; i <= total_seg; i++) {
        if (i != total_seg)
            sgmt.head.length = MAX_SEG_SIZE;
        else {
            if (fsize % MAX_SEG_SIZE != 0)
                sgmt.head.length = fsize % MAX_SEG_SIZE;
            else
                sgmt.head.length = MAX_SEG_SIZE;
        }
        sgmt.head.seqNumber = i;
        sgmt.head.ackNumber = 0;
        sgmt.head.sackNumber = 0;
        sgmt.head.fin = 0;
        sgmt.head.syn = 0;
        sgmt.head.ack = 0;
        sgmt.head.sent = 0;
        memset(sgmt.data, 0, sizeof(char) * MAX_SEG_SIZE);
        memcpy(sgmt.data, data_ptr, sgmt.head.length);
        sgmt.head.checksum = crc32(0L, (const Bytef *)sgmt.data, MAX_SEG_SIZE);

        Queue[i] = sgmt;
        data_ptr = data_ptr + sgmt.head.length;
    }
    segment finseg = { .head.fin=1, .head.seqNumber=total_seg+1 };
    finseg.head.checksum = crc32(0L, (const Bytef *)finseg.data, MAX_SEG_SIZE);
    Queue[total_seg+1] = finseg;
}

void initialize(int sock_fd, struct sockaddr_in *recv_addr) {
    /* Initialization */
    cwnd = 1.0;
    thresh = 2;
    dup_ack = 0;
    
    int old_seq[1] = {0};
    transmit_new(sock_fd, recv_addr, old_seq, 1);
    reset_timer();
    set_state(0);
}

void transmit_new(int sock_fd, struct sockaddr_in *recv_addr, int old_seqs[], int old_cwnd) {
    /* Transmit new segment of the specified seq number */
    // Identify new segments to be sent
    // printf("Old sequence:\n");
    // for (int i = 0; i < old_cwnd; i++) printf("%d ", old_seqs[i]);
    // printf("\n");

    segment *segs_to_be_sent[MAX_SEG_QUE_SIZE] = {};
    int k = 0, dup = 0;
    for (int i = 1; i <= (int)cwnd; i++) {
        dup = 0;
        for (int j = 0; j < old_cwnd; j++) {
            if ((Queue[i].head.seqNumber == old_seqs[j]) || (Queue[i].head.seqNumber == 0) || (Queue[i].head.fin == 1)) {
                dup = 1;
                break;
            }
        }

        if (!dup) {
            segs_to_be_sent[k++] = &Queue[i];
        }
    }

    // Send out new segments fallen into cwnd
    segment *sgmt;
    ssize_t byt;
    for (int i = 0; i < k; i++) {
        sgmt = segs_to_be_sent[i];
        byt = sendto(sock_fd, sgmt, sizeof(segment), 0, (struct sockaddr *)recv_addr, sizeof(struct sockaddr));

        // logging
        if (sgmt->head.sent == 0)
            printf("send\tdata\t#%d,\twinSize = %d\n", sgmt->head.seqNumber, (int)cwnd);
        else
            printf("resnd\tdata\t#%d,\twinSize = %d\n", sgmt->head.seqNumber, (int)cwnd);

        sgmt->head.sent = 1;
    }
}

void transmit_missing(int sock_fd, struct sockaddr_in *recv_addr) {
    /* (Re)transmit the first segment in the window. */
    segment sgmt = Queue[1];
    sendto(sock_fd, &sgmt, sizeof(sgmt), 0, (struct sockaddr *)recv_addr, sizeof(struct sockaddr));

    // logging
    if (sgmt.head.sent == 0)
        printf("send\tdata\t#%d,\twinSize = %d\n", sgmt.head.seqNumber, (int)cwnd);
    else
        printf("resnd\tdata\t#%d,\twinSize = %d\n", sgmt.head.seqNumber, (int)cwnd);

    Queue[1].head.sent = 1;
}

void reset_timer() {
    /* Reset timer */
    tick = clock();
}

void set_state(int flag) {
    /* Set to a specific state */
    if (flag == 0)
        state = SLOW_START;
    else
        state = CONGEST_AVOID;
}

void mark_sack(int sack_num) {
    /* Remove the segment with sequence number sack_num from the transmit queue */
    int idx = 0, found = 0;
    if (sack_num != 0) {
        for (; idx <= MAX_SEG_QUE_SIZE; idx++) {
            if ((idx != 0) && (Queue[idx].head.seqNumber == 0)) break;
            if (Queue[idx].head.seqNumber == sack_num) {
                found = 1; break;
            }
        }
    }

    if (found) {
        for (int i = idx; i < MAX_SEG_QUE_SIZE; i++) {
            if (i == MAX_SEG_QUE_SIZE) {
                memset(&Queue[i], 0, sizeof(segment));
                break;
            } else if ((Queue[i].head.seqNumber == 0) && (Queue[i+1].head.seqNumber == 0))
                break;

            Queue[i] = Queue[i+1];
        }
    }

    // printf("After mark_sack Queue:\n");
    // for (int i = 0; i < 6; i++) printf("%d ", Queue[i].head.seqNumber);
    // printf("\n");
}

void update_base(int ack_num) {
    /* Update transmit queue and cwnd s.t. base > ack_num */
    int idx = 0, found = 0;
    if (ack_num != 0) {
        for (; idx <= MAX_SEG_QUE_SIZE; idx++) {
            if ((idx != 0) && (Queue[idx].head.seqNumber == 0))
                break;
            if (Queue[idx].head.seqNumber == ack_num) {
                found = 1; break;
            }
        }
    }

    if (found) {
        for (int i = 1; i <= MAX_SEG_QUE_SIZE; i++) {
            if (i + idx == MAX_SEG_QUE_SIZE) {
                memset(&Queue[i], 0, idx*sizeof(segment));
                break;
            } else if ((Queue[i].head.seqNumber == 0) && (Queue[i+1].head.seqNumber == 0))
                break;
            
            Queue[i] = Queue[i + idx];
        }
    }

    // printf("After update_base Queue:\n");
    // for (int i = 0; i < 6; i++) printf("%d ", Queue[i].head.seqNumber);
    // printf("\n");
}

void timeout(int sock_fd, struct sockaddr_in *recv_addr) {
    // logging
    printf("time\tout,\tthreshold = %d,\twinSize = %d\n", thresh, (int)cwnd);

    thresh = MAX(1, (int)(cwnd/2));
    cwnd = 1.0;
    dup_ack = 0;
    transmit_missing(sock_fd, recv_addr);
    reset_timer();
    set_state(0);

}

// FSM Events Callback
void receive_ack(int sock_fd, struct sockaddr_in *recv_addr) {
    /* Receive ack segments from the receiver */
    /* Return the last ack num */
    ssize_t byt;
    int n_ack;
    socklen_t recv_addr_sz;
    segment *ack_segs = malloc(MAX_SEG_QUE_SIZE*sizeof(segment));
    byt = recvfrom(
        sock_fd, ack_segs, MAX_SEG_QUE_SIZE*sizeof(segment), 0, (struct sockaddr *)recv_addr, &recv_addr_sz
    );
    n_ack = byt / sizeof(segment);
    // printf("byt = %d, sizeof seg = %d, n_ack = %d\n", (int)byt, (int)sizeof(segment), n_ack);
    if (byt < 0) {
        fprintf(stderr, "Receive error.\n"); exit(1);
    }

    // Call dupAck() or newAck()
    segment sgmt = {};
    for (int i = 0; i < n_ack; i++) {
        sgmt = ack_segs[i];
        // logging
        printf("recv\tack\t#%d,\tsack\t#%d\n", sgmt.head.ackNumber, sgmt.head.sackNumber);

        if (sgmt.head.ackNumber < Queue[1].head.seqNumber) {
            // printf("**Duplicate ack**\n");
            recv_dup_ack(sock_fd, recv_addr, sgmt);
        } else {
            // printf("**New ack**\n");
            recv_new_ack(sock_fd, recv_addr, sgmt);
        }
    }

    free(ack_segs);
}

void recv_dup_ack(int sock_fd, struct sockaddr_in *recv_addr, segment sgmt) {
    /* Receive duplicate cumulative ack */
    // Give current segment numbers in current window
    int current_wnd_seqs[MAX_SEG_QUE_SIZE] = {0};
    for (int i = 0; i < (int)cwnd; i++) {
        current_wnd_seqs[i] = Queue[i+1].head.seqNumber;
    }

    dup_ack = dup_ack + 1;
    mark_sack(sgmt.head.sackNumber);

    transmit_new(sock_fd, recv_addr, current_wnd_seqs, (int)cwnd);
    if (dup_ack == 3) {
        transmit_missing(sock_fd, recv_addr);
    }
}

void recv_new_ack(int sock_fd, struct sockaddr_in *recv_addr, segment sgmt) {
    /* Receive new cumulative ack */
    // Give current segment numbers in current window
    int current_wnd_seqs[MAX_SEG_QUE_SIZE] = {0};
    for (int i = 0; i < (int)cwnd; i++) {
        current_wnd_seqs[i] = Queue[i+1].head.seqNumber;
    }

    mark_sack(sgmt.head.sackNumber);
    update_base(sgmt.head.ackNumber);
    int old_cwnd = (int)cwnd;
    if (state == SLOW_START) {
        cwnd++;
        if (cwnd >= thresh)
            set_state(1);
    } else if (state == CONGEST_AVOID)
        cwnd = cwnd + (1.0 / (int)cwnd);

    transmit_new(sock_fd, recv_addr, current_wnd_seqs, old_cwnd);
    reset_timer();
}

int send_fin(int sock_fd, struct sockaddr_in *recv_addr) {
    /* Send the fin segment to the receiver */
    sendto(sock_fd, &Queue[1], sizeof(segment), 0, (struct sockaddr *)recv_addr, sizeof(struct sockaddr));
    // logging
    printf("send\tfin\n");

    // Receive finack from receiver
    segment finack_sgmt;
    socklen_t recv_addr_sz;
    recvfrom(
        sock_fd, &finack_sgmt, sizeof(segment), 0, (struct sockaddr *)recv_addr, &recv_addr_sz
    );

    if ((finack_sgmt.head.fin == 1) && (finack_sgmt.head.ack == 1)) {
        // logging
        printf("recv\tfinack\n");
        return 0;
    } else
        return -1;
}
