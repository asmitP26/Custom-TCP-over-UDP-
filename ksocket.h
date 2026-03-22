#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/shm.h>
#include <time.h>

#define SOCK_KTP    5
#define MAX_SOCKETS 10
#define BUFFER_SIZE 10
#define MESSAGE_SIZE 512

#define ENOTBOUND  1001
#define ENOSPACE   1002
#define ENOMESSAGE 1003

#define T 5
#define P 0.40

#define DATA 1
#define ACK  2

// Socket lifecycle states (stored in KTP_Socket.state) 
#define SOCK_STATE_FREE    0
#define SOCK_STATE_CLAIMED 1
#define SOCK_STATE_BOUND   2   // k_bind done, waiting for initksocket to create UDP socket and move to ready
#define SOCK_STATE_READY   3   // UDP socket live; normal operation 

long k_get_total_transmissions(void);  // in ksocket.h

typedef struct {
    int  type;
    int  seq_no;
    int  rwnd;
    int  len; 
    char data[MESSAGE_SIZE];
} KTP_Message;

typedef struct {
    int    size;               // current window size = receiver free space
    int    seq_numbers[BUFFER_SIZE];
    int    next_seq_no;        // next seq# to assign to a new message 
    time_t send_time[BUFFER_SIZE];  // 0 = not yet transmitted 
} Sendwindow;

typedef struct {
    int size;                  // free slots remaining in recv buffer
} Recvwindow;

typedef struct {
    int state;                 // SOCK_STATE_
    int pid;
    int udp_socket;            // fd valid ONLY in initksocket process

    struct sockaddr_in src_addr;
    struct sockaddr_in dest_addr;

    // Send side 
    char send_buffer[BUFFER_SIZE][MESSAGE_SIZE]; // it store the outgoing messages until they are ACKed
    int  send_buffer_len[BUFFER_SIZE];  // track message lengths in send buffer
    Sendwindow swnd;

    // Receive side
    char recv_buffer[BUFFER_SIZE][MESSAGE_SIZE]; // store incoming message 
    int  recv_buffer_len[BUFFER_SIZE];   //receive message size
    int  recv_seq[BUFFER_SIZE];    // seq# of each stored recv message
    Recvwindow rwnd;

    int  recv_next_app;    // next seq# k_recvfrom should return to appl.
    int  recv_last_ack;    // last cumulative seq# ACKed to sender

    int  nospace;          // 1 = recv buffer was full when last checked
} KTP_Socket;

typedef struct {
    KTP_Socket sockets[MAX_SOCKETS];
    long total_transmissions; 
} sharedmemory;

// API 
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, char *src_ip, int src_port, char *dest_ip, int dest_port);
int k_sendto(int sockfd, char *msg, int len);
int k_recvfrom(int sockfd, char *buffer);
int k_close(int sockfd);
int dropmessage(float p);

#endif