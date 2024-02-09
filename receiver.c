#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <fcntl.h>         // open()
#include <sys/stat.h>      // stat()
#include <zlib.h>

#include "def.h"


// Global Variables
segment BUFFER[MAX_SEG_BUF_SIZE+1];         // Receiver's buffer, BUFFER[0] is a dummy.
segment STORED_SEGS[MAX_SEG_QUE_SIZE];      // Stored file data, STORED_SEGS[0] is a dummy
uint8_t APP_DATA[BUF_SIZE];                 // File data received
int VRANGE_MIN, VRANGE_MAX;                 // Current valid sequence number range, inclusive.


// Functions
void setIP(char *dst, char *src);
void initialize();
int is_corrupt(segment sgmt);
void send_sack(int sock_fd, struct sockaddr_in *recv_addr, int ack_num, int sack_num, int is_fin);
void update_base(int *base, segment sgmt);
void mark_sack(segment sgmt);
int is_all_received();
int is_buffer_full();
int is_over_buffer(int seq);
void flush_buffer(uint8_t **app_data_ptr, EVP_MD_CTX *sha_obj);
void hex_digest(const void *buf, int len, char *hash);
void compute_sha256(EVP_MD_CTX *sha_obj, uint8_t *data_ptr, int sz, int total_sz);
void end_receive(uint8_t *app_data_ptr, int sock_fd);

void save_file(char *filepath, int data_sz);


// ./receiver <recv_ip> <recv_port> <agent_ip> <agent_port> <dst_filepath>
int main(int argc, char *argv[]) {
    // parse arguments
    if (argc != 6) {
        fprintf(stderr, "Usage: ./receiver <recv_ip> <recv_port> <agent_ip> <agent_port> <dst_filepath>\n");
        exit(1);
    }

    int recv_port, agent_port;
    char recv_ip[50], agent_ip[50];

    // read argument
    setIP(recv_ip, argv[1]);
    sscanf(argv[2], "%d", &recv_port);

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
    addr.sin_port = htons(recv_port);
    addr.sin_addr.s_addr = inet_addr(recv_ip);
    memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));    
    bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    
    // Initialization
    initialize();
    EVP_MD_CTX *sha256 = EVP_MD_CTX_new();
    EVP_DigestInit_ex(sha256, EVP_sha256(), NULL);

    int base = 1;
    ssize_t byt;
    socklen_t recv_addr_sz;
    segment sgmt = {};
    uint8_t *app_data_ptr = APP_DATA;

    // Main loop
    while (1) {
        // printf("\nNew loop-->\n");
        // printf("\nInitial BUFFER:\n");
        // for (int i = 0; i < 10; i++) printf("%d ", BUFFER[i].head.seqNumber);
        // printf("\nbase = %d\n", base);
        // printf("\n");

        memset(&sgmt, 0, sizeof(segment));
        byt = recvfrom(
            sock_fd, &sgmt, sizeof(segment), 0, (struct sockaddr *)&recv_addr, &recv_addr_sz
        );
        // printf("byt = %d\n", (int)byt);

        if (is_corrupt(sgmt)) {
            printf("drop\tdata\t#%d\t(corrupted)\n", sgmt.head.seqNumber);
            send_sack(sock_fd, &recv_addr, base-1, base-1, 0);
        }
        else if (sgmt.head.seqNumber == base) {
            // In-order segments
            if (sgmt.head.fin != 1)
                printf("recv\tdata\t#%d\t(in order)\n", sgmt.head.seqNumber);

            update_base(&base, sgmt);
            send_sack(sock_fd, &recv_addr, base-1, sgmt.head.seqNumber, sgmt.head.fin);
            if (is_all_received()) {
                flush_buffer(&app_data_ptr, sha256);
                end_receive(app_data_ptr, sock_fd);
                break;
            } else if (is_buffer_full()) {
                flush_buffer(&app_data_ptr, sha256);
            }
        }
        else {
            // Out-of-order segment
            if (is_over_buffer(sgmt.head.seqNumber)) {
                // Drop
                printf("drop\tdata\t#%d\t(buffer overflow)\n", sgmt.head.seqNumber);
                send_sack(sock_fd, &recv_addr, base-1, base-1, 0);
            } else {
                printf("recv\tdata\t#%d\t(out of order, sack-ed)\n", sgmt.head.seqNumber); 
                mark_sack(sgmt);
                send_sack(sock_fd, &recv_addr, base-1, sgmt.head.seqNumber, sgmt.head.fin);
            }
        }
    }

    EVP_MD_CTX_free(sha256);
    save_file(filepath, app_data_ptr - APP_DATA);
    return 0;
}

// Functions
void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0 || strcmp(src, "localhost") == 0){
        sscanf("127.0.0.1", "%s", dst);
    }
    else{
        sscanf(src, "%s", dst);
    }
    return;
}

void initialize() {
    /* Initialization */
    memset(BUFFER, 0, (MAX_SEG_BUF_SIZE+1) * sizeof(segment));
    memset(STORED_SEGS, 0, MAX_SEG_QUE_SIZE * sizeof(segment));
    memset(APP_DATA, 0, BUF_SIZE * sizeof(uint8_t));
    VRANGE_MIN = 1; VRANGE_MAX = MAX_SEG_BUF_SIZE;
}

int is_corrupt(segment sgmt) {
    /* Return 1 if the segment is corrupted */
    unsigned int correct_ck;
    correct_ck = crc32(0L, (const Bytef *)sgmt.data, MAX_SEG_SIZE);
    if (correct_ck == sgmt.head.checksum)
        return 0;
    else
        return 1;
}

void send_sack(int sock_fd, struct sockaddr_in *recv_addr, int ack_num, int sack_num, int is_fin) {
    /* Send an ack segment */
    segment sgmt = {};
    if (is_fin) {
        printf("recv\tfin\n");
        sgmt.head.fin = 1;
        sgmt.head.ack = 1;
    } else {
        sgmt.head.ackNumber = ack_num;
        sgmt.head.sackNumber = sack_num;
        sgmt.head.ack = 1;
    }
    sendto(sock_fd, &sgmt, sizeof(sgmt), 0, (struct sockaddr *)recv_addr, sizeof(struct sockaddr));

    if (!is_fin)
        printf("send\tack\t#%d,\tsack\t#%d\n", ack_num, sack_num);
    else
        printf("send\tfinack\n");
}

void update_base(int *base, segment sgmt) {
    /* Update base and BUFFER s.t. base is the first unsacked packet */
    int idx = sgmt.head.seqNumber - VRANGE_MIN + 1;
    BUFFER[idx] = sgmt;

    if (BUFFER[1].head.seqNumber != 0) {
        for (int i = 1; i <= MAX_SEG_BUF_SIZE; i++) {
            if ((i == MAX_SEG_BUF_SIZE) && (BUFFER[i].head.seqNumber != 0)) {
                *base = BUFFER[i].head.seqNumber + 1;
                break;
            } else if (BUFFER[i].head.seqNumber == 0) {
                *base = BUFFER[i-1].head.seqNumber + 1;
                break;
            }
        }
    }
}

void mark_sack(segment sgmt) {
    /* Place the segment in the buffer */
    int idx;
    idx = sgmt.head.seqNumber - VRANGE_MIN + 1;
    BUFFER[idx] = sgmt;
}

int is_all_received() {
    /* Check if all segments are received */
    int last_seq = 0;
    for (int i = 1; i <= MAX_SEG_QUE_SIZE; i++) {
        if (STORED_SEGS[i].head.seqNumber == 0) {
            last_seq = STORED_SEGS[i-1].head.seqNumber;
            break;
        }
        if (STORED_SEGS[i].head.seqNumber != i) {
            return 0;
        }
    }

    for (int j = 1; j <= MAX_SEG_BUF_SIZE; j++) {
        if (BUFFER[j].head.fin == 1)
            return 1;
        if (BUFFER[j].head.seqNumber != last_seq + j) {
            return 0;
        }
    }
    return 0;
}

int is_buffer_full() {
    /* 1 if BUFFER is full */
    for (int i = 1; i <= MAX_SEG_BUF_SIZE; i++) {
        if (BUFFER[i].head.seqNumber == 0)
            return 0;
    }
    return 1;
}

int is_over_buffer(int seq) {
    /* 1 if the received segment has seqNumber > VRANGE_MAX */
    if (seq > VRANGE_MAX)
        return 1;
    return 0;
}

void flush_buffer(uint8_t **app_data_ptr, EVP_MD_CTX *sha_obj) {
    /* Flush the BUFFER to the STORED_SEGS, and copy the data to the APP_DATA */
    /* And compute the SHA256 */
    printf("flush\n");

    int i, n_seg = 0, first_seq;
    first_seq = BUFFER[1].head.seqNumber;
    for (i = 1; i <= MAX_SEG_BUF_SIZE; i++) {
        if (BUFFER[i].head.seqNumber == 0) {    // do not include fin packet
            n_seg = i - 1;
            break;
        }
    }
    if (i == MAX_SEG_BUF_SIZE+1) n_seg = MAX_SEG_BUF_SIZE;

    // Append new segments to STORED_SEGS
    if (n_seg != 0)
        memcpy(&STORED_SEGS[first_seq], &BUFFER[1], n_seg * sizeof(segment));

    // Copy the data from the BUFFER to APP_DATA
    int sz = 0, total_sz = 0;
    uint8_t *orig_data_ptr = *app_data_ptr; 
    for (int j = 1; j <= n_seg; j++) {
        sz = sz + BUFFER[j].head.length;
        memcpy(*app_data_ptr, BUFFER[j].data, BUFFER[j].head.length);
        *app_data_ptr = *app_data_ptr + BUFFER[j].head.length;
    }
    total_sz = (*app_data_ptr - APP_DATA) * sizeof(uint8_t);

    // Compute SHA256
    compute_sha256(sha_obj, orig_data_ptr, sz, total_sz);

    // Modify the valid range of sequence number
    VRANGE_MIN = VRANGE_MIN + MAX_SEG_BUF_SIZE;
    VRANGE_MAX = VRANGE_MAX + MAX_SEG_BUF_SIZE;

    // Clear buffer
    memset(BUFFER, 0, (MAX_SEG_BUF_SIZE + 1) * sizeof(segment));
}

void hex_digest(const void *buf, int len, char *hash) {
    /* Convert char to hex string */
    const unsigned char *cbuf = (const unsigned char*)(buf);
    int p = 0;

    for (int i = 0; i != len; ++i) {
        sprintf(hash+p, "%.2x", (unsigned int)cbuf[i]);
        p = p + 2;
    }
    hash[p] = '\0';
}

void compute_sha256(EVP_MD_CTX *sha_obj, uint8_t *data_ptr, int sz, int total_sz) {
    /* Compute the hash of given data and print it out */
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    // update the object given a buffer and length
    EVP_DigestUpdate(sha_obj, data_ptr, sz);

    // calculating hash
    // (we need to make a copy of `sha256` for EVP_DigestFinal_ex to use,
    // otherwise `sha256` will be broken)
    EVP_MD_CTX *tmp_sha256 = EVP_MD_CTX_new();
    EVP_MD_CTX_copy_ex(tmp_sha256, sha_obj);
    EVP_DigestFinal_ex(tmp_sha256, hash, &hash_len);
    EVP_MD_CTX_free(tmp_sha256);

    // print hash
    char hex[1024] = {};
    hex_digest(hash, hash_len, hex);
    printf("sha256\t%d\t%s\n", total_sz, hex); 
}

void end_receive(uint8_t *app_data_ptr, int sock_fd) {
    /* End the connection and compute finsha */
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    // make a SHA256 object and initialize
    EVP_MD_CTX *sha256 = EVP_MD_CTX_new();
    EVP_DigestInit_ex(sha256, EVP_sha256(), NULL);

    EVP_DigestUpdate(sha256, APP_DATA, (app_data_ptr - APP_DATA) * sizeof(uint8_t));
    EVP_MD_CTX *tmp_sha256 = EVP_MD_CTX_new();
    EVP_MD_CTX_copy_ex(tmp_sha256, sha256);
    EVP_DigestFinal_ex(tmp_sha256, hash, &hash_len);
    EVP_MD_CTX_free(tmp_sha256);
    EVP_MD_CTX_free(sha256);

    // Print out the hash
    char hex[1024] = {};
    hex_digest(hash, hash_len, hex);
    printf("finsha\t%s\n", hex);

    // Close the connection
    close(sock_fd);
}

void save_file(char *filepath, int data_sz) {
    /* Save the transmitted file to the specified datapath */
    // Create dir if not exist
    // Find dir name
    char dir_name[100], *p;
    strcpy(dir_name, filepath);
    p = strrchr(dir_name, '/');
    if (p) {
        *(p + 1) = '\0';

        struct stat st = {0};
        if (stat(dir_name, &st) == -1) mkdir(dir_name, 0777);
    }

    // Write output file
    int fd = open(filepath, O_WRONLY | O_CREAT);
    chmod(filepath, 0777);
    ssize_t byt = write(fd, APP_DATA, data_sz);
    if (byt < 0) { fprintf(stderr, "File output error.\n"); }

    close(fd);
}
