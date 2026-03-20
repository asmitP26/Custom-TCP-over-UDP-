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

#define SOCK_KTP 5
#define MAX_SOCKETS 10
#define BUFFER_SIZE 10
#define MESSAGE_SIZE 512
#define ENOTBOUND 1001
#define ENOSPACE  1002
#define ENOMESSAGE  1003

#define T 5
#define P 0.1

// for type part of KTP_Message
#define DATA 1
#define ACK 2

// message structure for KTP protocol
typedef struct {
    int type;
    int seq_no;
    int rwnd;
    char data[MESSAGE_SIZE];
} KTP_Message;

// sender window structure
typedef struct {
    int size;
    int seq_numbers[BUFFER_SIZE];
    int next_seq_no;
    time_t send_time[BUFFER_SIZE];
} Sendwindow;

// receiver window structure
typedef struct {
    int size;
    int seq_numbers[BUFFER_SIZE];
} Recvwindow;

//KTP socket entry as described in the project pdf
typedef struct {

    int is_free;      // status 1 for free , 0 for occupied
    int pid;          // process id 

    int udp_socket;   // udp socket file descriptor

    struct sockaddr_in src_addr;  // source address
    struct sockaddr_in dest_addr; // destination address

    char send_buffer[BUFFER_SIZE][MESSAGE_SIZE]; //messages waiting to be transmitted
    char recv_buffer[BUFFER_SIZE][MESSAGE_SIZE]; //messages received but not yet read by application

    Sendwindow swnd;   // sending window   
    Recvwindow rwnd;   // receiving window 

    int nospace;       // flag to indicate receiver buffer is full or not 

} KTP_Socket;

typedef struct {
    KTP_Socket sockets[MAX_SOCKETS];
} sharedmemory ;

int k_socket(int domain, int type, int protocol); // it give sockfd

int k_bind(int sockfd,
           char *src_ip,
           int src_port,
           char *dest_ip,
           int dest_port);

int k_sendto(int sockfd, char *msg, int len);

int k_recvfrom(int sockfd, char *buffer);

int k_close(int sockfd);

int dropmessage(float p);


#endif