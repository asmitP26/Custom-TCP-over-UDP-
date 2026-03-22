#include "ksocket.h"
#include <errno.h>
#include <string.h>

sharedmemory *SM;

static sharedmemory *get_sm(void)
{
    if (SM)
    {
        return SM;
    }
    key_t key = ftok("ksocket.h", 65); // create a shared memory key , all program use the same key to access the same shared memory segment
    int shmid = shmget(key, sizeof(sharedmemory), 0666);
    if (shmid < 0)
    {
        perror("get_sm: shmget");
        return NULL;
    }
    SM = (sharedmemory *)shmat(shmid, NULL, 0); // attach the shared memory segment to the process's address space
    if (SM == (void *)-1)
    {
        perror("get_sm: shmat");
        SM = NULL;
    }
    return SM;
}
int dropmessage(float p)
{
    return ((double)rand() / RAND_MAX) < p ? 1 : 0;
}

int k_socket(int domain, int type, int protocol)
{
    if (type != SOCK_KTP) // only support SOCK_KTP type
    {
        errno = EINVAL;
        return -1;
    }
    if (!get_sm()) // connect process to shared memory segment, if fail return -1
        return -1;

    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        if (SM->sockets[i].state == SOCK_STATE_FREE)
        {
            SM->sockets[i].state = SOCK_STATE_CLAIMED; // mark the socket as claimed, but not yet bound
            SM->sockets[i].pid = getpid();             // store the pid of the process that owns this socket
            SM->sockets[i].udp_socket = -1;            // initialize the UDP socket to -1
            SM->sockets[i].nospace = 0;                // initialize the nospace flag to 0
            SM->sockets[i].swnd.size = BUFFER_SIZE;    // initialize the send window size to the buffer size
            SM->sockets[i].swnd.next_seq_no = 1;       // initialize the next sequence number to 1
            SM->sockets[i].rwnd.size = BUFFER_SIZE;    // initialize the receive window size to the buffer size
            SM->sockets[i].recv_next_app = 1;          // initialize the next expected sequence number for the application to 1
            SM->sockets[i].recv_last_ack = 0;          // initialize the last acknowledged sequence number to 0
            for (int j = 0; j < BUFFER_SIZE; j++)
            {
                SM->sockets[i].swnd.seq_numbers[j] = 0;
                SM->sockets[i].swnd.send_time[j] = 0;
                SM->sockets[i].send_buffer_len[j] = 0;
                SM->sockets[i].recv_buffer_len[j] = 0;
                SM->sockets[i].recv_seq[j] = 0;
            }
            return i;
        }
    }
    errno = ENOSPACE;
    return -1;
}

int k_bind(int sockfd, char *src_ip, int src_port, char *dest_ip, int dest_port)
{
    if (sockfd < 0 || sockfd >= MAX_SOCKETS)
        return -1;
    if (!get_sm())
        return -1;

    struct sockaddr_in src, dst;
    memset(&src, 0, sizeof(src));
    src.sin_family = AF_INET;
    src.sin_port = htons(src_port);
    inet_pton(AF_INET, src_ip, &src.sin_addr);

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip, &dst.sin_addr);

    SM->sockets[sockfd].src_addr = src;
    SM->sockets[sockfd].dest_addr = dst;
    SM->sockets[sockfd].state = SOCK_STATE_BOUND;

    // Spin until initksocket creates + binds the UDP socket 
    struct timespec ts = {0, 5000000};
    for (int i = 0; i < 2000; i++)
    {
        if (SM->sockets[sockfd].state == SOCK_STATE_READY)
            return 0;
        nanosleep(&ts, NULL);
    }
    fprintf(stderr, "k_bind: timeout waiting for initksocket\n");
    return -1;
}

int k_sendto(int sockfd, char *msg, int len)
{
    if (sockfd < 0 || sockfd >= MAX_SOCKETS)
        return -1;
    if (!get_sm())
        return -1;
    if (SM->sockets[sockfd].state != SOCK_STATE_READY)
    {
        errno = ENOTBOUND;
        return -1;
    }

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (SM->sockets[sockfd].send_buffer_len[i] == 0)
        {
            memcpy(SM->sockets[sockfd].send_buffer[i], msg, len);
            SM->sockets[sockfd].send_buffer_len[i] = len;
            SM->sockets[sockfd].swnd.seq_numbers[i] = SM->sockets[sockfd].swnd.next_seq_no++;
            SM->sockets[sockfd].swnd.send_time[i] = 0;
            return len;
        }
    }
    errno = ENOSPACE;
    return -1;
}

int k_recvfrom(int sockfd, char *buffer)
{
    if (sockfd < 0 || sockfd >= MAX_SOCKETS)
        return -1;
    if (!get_sm())
        return -1;

    int want = SM->sockets[sockfd].recv_next_app;

    // Find the slot that holds the next expected message 
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (SM->sockets[sockfd].recv_buffer_len[i] > 0 &&
            SM->sockets[sockfd].recv_seq[i] == want)
        {
            int len = SM->sockets[sockfd].recv_buffer_len[i];
            memcpy(buffer, SM->sockets[sockfd].recv_buffer[i], len);
            memset(SM->sockets[sockfd].recv_buffer[i], 0, MESSAGE_SIZE);
            SM->sockets[sockfd].recv_buffer_len[i] = 0;
            SM->sockets[sockfd].recv_seq[i] = 0;
            SM->sockets[sockfd].rwnd.size++;
            SM->sockets[sockfd].recv_next_app++;
            return len;
        }
    }
    errno = ENOMESSAGE;
    return -1;
}

int k_close(int sockfd)
{
    if (sockfd < 0 || sockfd >= MAX_SOCKETS)
        return -1;
    if (!get_sm())
        return -1;

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        memset(SM->sockets[sockfd].send_buffer[i], 0, MESSAGE_SIZE);
        SM->sockets[sockfd].send_buffer_len[i] = 0;
        memset(SM->sockets[sockfd].recv_buffer[i], 0, MESSAGE_SIZE);
        SM->sockets[sockfd].recv_buffer_len[i] = 0;
        SM->sockets[sockfd].recv_seq[i] = 0;
    }
    SM->sockets[sockfd].state = SOCK_STATE_FREE;
    SM->sockets[sockfd].pid = -1;
    SM->sockets[sockfd].swnd.next_seq_no = 1;
    SM->sockets[sockfd].rwnd.size = BUFFER_SIZE;
    SM->sockets[sockfd].recv_next_app = 1;
    SM->sockets[sockfd].recv_last_ack = 0;
    SM->sockets[sockfd].nospace = 0;
    return 0;
}
long k_get_total_transmissions(void) {
    return SM->total_transmissions;
}
/*
pkill initksocket
pkill user2
ipcs -m | awk 'NR>3{print $2}' | xargs ipcrm -m
*/
