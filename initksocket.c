/*
Mini Project 1 Submission
Group Details:
Member 1 Name: Asmit Pandey
Member 1 Roll number: 23CS10006
Member 2 Name: Harhshwardhan Repaswal
Member 2 Roll number: 23CS30022
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>
#include "ksocket.h"
long total_transmissions = 0;
extern sharedmemory *SM;

// create and bind a UDP socket inside the initksocket process
static void setup_udp_socket(int i)
{

    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0)
    {
        perror("setup_udp_socket");
        return;
    }
    int opt = 1;
    // set socket options to reuse port
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(fd, (struct sockaddr *)&SM->sockets[i].src_addr, sizeof(SM->sockets[i].src_addr)) < 0) // bind socket to source address
    {
        perror("setup_udp_socket: bind");
        close(fd);
        return;
    }
    SM->sockets[i].udp_socket = fd;                                                                       // save socket file descriptor
    SM->sockets[i].state = SOCK_STATE_READY;                                                              // mark socket as ready
    printf("initksocket: slot %d bound port %d fd=%d\n", i, ntohs(SM->sockets[i].src_addr.sin_port), fd); // mark socket as ready
    fflush(stdout);
}
// thread R
void *thread_R(void *arg)
{
    printf("thread_R: started\n");
    fflush(stdout);

    // main loop
    while (1)
    {
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            if (SM->sockets[i].state == SOCK_STATE_BOUND)
                setup_udp_socket(i);
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;

        for (int i = 0; i < MAX_SOCKETS; i++)
        {

            if (SM->sockets[i].state == SOCK_STATE_READY) // if socket is ready
            {

                FD_SET(SM->sockets[i].udp_socket, &rfds); // add to read set

                if (SM->sockets[i].udp_socket > maxfd) // update max file descriptor
                    maxfd = SM->sockets[i].udp_socket;
            }
        }
        // if no ready sockets, sleep and continue
        if (maxfd == -1)
        {
            usleep(50000);
            continue;
        }

        // timeout for select: 200ms
        struct timeval tv = {0, 200000};
        int act = select(maxfd + 1, &rfds, NULL, NULL, &tv); // check which sockets have data to read
        if (act < 0)
            continue;

        if (act == 0) // if timeout occurred (no data received)
        {

            for (int i = 0; i < MAX_SOCKETS; i++) // timeout checks
            {

                if (SM->sockets[i].state == SOCK_STATE_BOUND) // if socket just became bound, setup UDP socket
                    setup_udp_socket(i);
                // if receive buffer was full and now has space, send cumulative ACK
                if (SM->sockets[i].state == SOCK_STATE_READY &&
                    SM->sockets[i].nospace && SM->sockets[i].rwnd.size > 0)
                {
                    // create ACK message
                    KTP_Message dup;
                    // clear message
                    memset(&dup, 0, sizeof(dup));
                    // set message type to ACK
                    dup.type = ACK;
                    // set sequence number to last received ACK
                    dup.seq_no = SM->sockets[i].recv_last_ack;
                    // set receive window size
                    dup.rwnd = SM->sockets[i].rwnd.size;
                    // send ACK via UDP
                    sendto(SM->sockets[i].udp_socket, &dup, sizeof(dup), 0,
                           (struct sockaddr *)&SM->sockets[i].dest_addr,
                           sizeof(SM->sockets[i].dest_addr));
                }
            }
            // go to next iteration
            continue;
        }

        // data received on one or more sockets
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            // skip if socket not ready
            if (SM->sockets[i].state != SOCK_STATE_READY)
                continue;
            // get socket file descriptor
            int fd = SM->sockets[i].udp_socket;
            // skip if socket not in read set
            if (!FD_ISSET(fd, &rfds))
                continue;

            // receive KTP message
            KTP_Message msg;
            // sender address
            struct sockaddr_in sender;
            // sender address length
            socklen_t slen = sizeof(sender);
            // receive message from socket
            if (recvfrom(fd, &msg, sizeof(msg), 0,
                         (struct sockaddr *)&sender, &slen) <= 0)
                continue;

            // randomly drop message based on probability P
            if (dropmessage(P))
                continue;

            // if message is data message
            if (msg.type == DATA)
            {
                SM->sockets[i].nospace = 0;
                int seq = msg.seq_no;

                // Go-Back-N: only accept next expected in-order message
                if (seq == SM->sockets[i].recv_last_ack + 1)
                {
                    // find free slot
                    int slot = -1;
                    for (int j = 0; j < BUFFER_SIZE; j++)
                    {
                        if (SM->sockets[i].recv_buffer_len[j] == 0)
                        {
                            slot = j;
                            break;
                        }
                    }

                    if (slot != -1)
                    {
                        // store message in buffer */
                        memcpy(SM->sockets[i].recv_buffer[slot], msg.data, msg.len);
                        SM->sockets[i].recv_buffer_len[slot] = msg.len;
                        SM->sockets[i].recv_seq[slot] = seq;
                        SM->sockets[i].rwnd.size--;
                        SM->sockets[i].recv_last_ack = seq;

                        if (SM->sockets[i].rwnd.size == 0)
                            SM->sockets[i].nospace = 1;
                    }
                }
                // out-of-order or duplicate: discard silently, still send ACK

                // always ACK with last in-order seq received
                KTP_Message ack;
                memset(&ack, 0, sizeof(ack));
                ack.type = ACK;
                ack.seq_no = SM->sockets[i].recv_last_ack;
                ack.rwnd = SM->sockets[i].rwnd.size;
                sendto(fd, &ack, sizeof(ack), 0,
                       (struct sockaddr *)&sender, slen);
            }
            else if (msg.type == ACK)
            {

                // cumulative ACK: free all send buffer entries with seq <= msg.seq_no

                // search through send buffer
                for (int j = 0; j < BUFFER_SIZE; j++)
                {
                    // if buffer has data and sequence is <= ack sequence
                    if (SM->sockets[i].send_buffer_len[j] > 0 &&
                        SM->sockets[i].swnd.seq_numbers[j] <= msg.seq_no)
                    {
                        // clear send buffer
                        memset(SM->sockets[i].send_buffer[j], 0, MESSAGE_SIZE);
                        // mark buffer slot as empty
                        SM->sockets[i].send_buffer_len[j] = 0;
                        // clear sequence number
                        SM->sockets[i].swnd.seq_numbers[j] = 0;
                        // clear send time
                        SM->sockets[i].swnd.send_time[j] = 0;
                    }
                }
                // update send window size from ACK
                SM->sockets[i].swnd.size = msg.rwnd;
                // fast retransmit: reset send_time of all pending messages
                // so thread_S sends them immediately on next wake-up 
                for (int j = 0; j < BUFFER_SIZE; j++)
                {
                    if (SM->sockets[i].send_buffer_len[j] > 0)
                        SM->sockets[i].swnd.send_time[j] = 0;
                }
            }
        }
    }
    // return from thread
    return NULL;
}
// send thread - handles retransmission and sending new messages
void *thread_S(void *arg)
{
    // print thread start message
    printf("thread_S: started\n");
    fflush(stdout);

    // main loop
    while (1)
    {
        // sleep time: T/2 seconds
        struct timespec ts = {1, 0};
        nanosleep(&ts, NULL);
        // get current time
        time_t now = time(NULL);

        // loop through all sockets
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            // skip if socket not ready
            if (SM->sockets[i].state != SOCK_STATE_READY)
                continue;
            // get socket file descriptor
            int fd = SM->sockets[i].udp_socket;

            // retransmit timed-out messages
            for (int j = 0; j < BUFFER_SIZE; j++)
            {
                // if message sent and timeout expired
                if (SM->sockets[i].send_buffer_len[j] > 0 &&
                    SM->sockets[i].swnd.send_time[j] != 0 &&
                    difftime(now, SM->sockets[i].swnd.send_time[j]) >= (SM->sockets[i].swnd.size == 0 ? 1 : T))
                {
                    // create message
                    KTP_Message m;
                    memset(&m, 0, sizeof(m));
                    m.type = DATA;
                    m.seq_no = SM->sockets[i].swnd.seq_numbers[j];
                    m.rwnd = SM->sockets[i].rwnd.size;
                    m.len = SM->sockets[i].send_buffer_len[j];
                    memcpy(m.data, SM->sockets[i].send_buffer[j], m.len);
                    sendto(fd, &m, sizeof(m), 0, (struct sockaddr *)&SM->sockets[i].dest_addr, sizeof(SM->sockets[i].dest_addr));
                    SM->total_transmissions++;
                    // update send time
                    SM->sockets[i].swnd.send_time[j] = now;
                }
            }

            // send new (unsent) messages, respecting window size
            int in_flight = 0;
            // flag for unsent messages
            int has_unsent = 0;
            // count in-flight and unsent messages
            for (int j = 0; j < BUFFER_SIZE; j++)
            {
                // if message sent but not ACKed
                if (SM->sockets[i].send_buffer_len[j] > 0 &&
                    SM->sockets[i].swnd.send_time[j] != 0)
                    // increment in-flight count
                    in_flight++;
                // if message not yet sent
                if (SM->sockets[i].send_buffer_len[j] > 0 &&
                    SM->sockets[i].swnd.send_time[j] == 0)
                    // set unsent flag
                    has_unsent = 1;
            }

            // zero-window probe: if receiver advertised rwnd=0 but we have pending data
            // and nothing in flight, send one probe message so receiver can reply with updated rwnd
            int effective_wnd = SM->sockets[i].swnd.size;
            // if window size is zero and we have unsent data and no in-flight messages
            if (effective_wnd == 0 && has_unsent && in_flight == 0)
                // create probe window
                effective_wnd = 1;

            // send unsent messages if window allows
            for (int j = 0; j < BUFFER_SIZE; j++)
            {
                // if buffer has data, not sent, and window allows
                if (SM->sockets[i].send_buffer_len[j] > 0 &&
                    SM->sockets[i].swnd.send_time[j] == 0 &&
                    effective_wnd > 0 &&
                    in_flight < effective_wnd)
                {
                    // create message
                    KTP_Message m;
                    memset(&m, 0, sizeof(m));
                    m.type = DATA;
                    m.seq_no = SM->sockets[i].swnd.seq_numbers[j];
                    m.rwnd = SM->sockets[i].rwnd.size;
                    m.len = SM->sockets[i].send_buffer_len[j];
                    memcpy(m.data, SM->sockets[i].send_buffer[j], m.len);
                    sendto(fd, &m, sizeof(m), 0, (struct sockaddr *)&SM->sockets[i].dest_addr, sizeof(SM->sockets[i].dest_addr));
                    SM->total_transmissions++;
                    // update send time
                    if (SM->sockets[i].swnd.size > 0)
                    {
                        SM->sockets[i].swnd.send_time[j] = now;
                        in_flight++;
                    }
                    // if zero-window probe: don't set send_time, so it retries every wake-up 
                }
            }
        }
    }
    // return from thread
    return NULL;
}

// garbage collector - cleans up dead processes
static void garbage_collector(void)
{
    // infinite loop
    while (1)
    {
        // sleep for 5 seconds
        sleep(5);
        // loop through all sockets
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            // skip if socket already free
            if (SM->sockets[i].state == SOCK_STATE_FREE)
                continue;
            // check if process is dead
            if (kill(SM->sockets[i].pid, 0) == -1 && errno == ESRCH)
            {
                if (SM->sockets[i].udp_socket >= 0)
                    close(SM->sockets[i].udp_socket);
                memset(&SM->sockets[i], 0, sizeof(KTP_Socket));
                SM->sockets[i].state = SOCK_STATE_FREE;
                SM->sockets[i].pid = -1;
                SM->sockets[i].udp_socket = -1;
                SM->sockets[i].rwnd.size = BUFFER_SIZE;
                SM->sockets[i].swnd.size = BUFFER_SIZE;
                printf("GC: freed slot %d\n", i);
                fflush(stdout);
            }
        }
    }
}

// main initialization function
int main(void)
{
    // create key for shared memory
    key_t key = ftok("ksocket.h", 65);
    // get old shared memory if exists
    int old = shmget(key, 0, 0666);
    // if old shared memory exists, remove it
    if (old >= 0)
        shmctl(old, IPC_RMID, NULL);
    int shmid = shmget(key, sizeof(sharedmemory), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0)
    {
        perror("shmget");
        exit(1);
    }

    // attach shared memory
    SM = (sharedmemory *)shmat(shmid, NULL, 0);
    // if attach failed
    if (SM == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }
    SM->total_transmissions = 0;
    printf("initksocket: ready (shmid=%d)\n", shmid);
    fflush(stdout);

    // loop through all sockets and initialize
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        // clear socket memory
        memset(&SM->sockets[i], 0, sizeof(KTP_Socket));
        SM->sockets[i].state = SOCK_STATE_FREE;
        SM->sockets[i].pid = -1;
        SM->sockets[i].udp_socket = -1;
        SM->sockets[i].rwnd.size = BUFFER_SIZE;
        SM->sockets[i].swnd.size = BUFFER_SIZE;    
        SM->sockets[i].swnd.next_seq_no = 1;      
        SM->sockets[i].recv_next_app = 1;
        SM->sockets[i].recv_last_ack = 0;
    }

    // thread IDs for receive and send threads
    pthread_t rt, st;
    // create receive thread
    pthread_create(&rt, NULL, thread_R, NULL);
    // create send thread
    pthread_create(&st, NULL, thread_S, NULL);

    // fork a new process for garbage collector
    pid_t gc = fork();
    // if in child process, run garbage collector
    if (gc == 0)
    {
        garbage_collector();
        exit(0);
    }

    // main loop - sleep indefinitely
    while (1)
        sleep(1000);
    // return 0
    return 0;
}