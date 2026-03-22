#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "ksocket.h"

int main()
{
    int sock;
    char buffer[MESSAGE_SIZE];

    sock = k_socket(AF_INET, SOCK_KTP, 0);
    if (sock < 0) {
        printf("socket creation failed\n");
        fflush(stdout);
        return 1;
    }
    printf("user2: socket created = %d\n", sock);
    fflush(stdout);

    if (k_bind(sock, "127.0.0.1", 6000, "127.0.0.1", 5000) < 0) {
        printf("k_bind failed\n");
        fflush(stdout);
        return 1;
    }
    printf("user2: bind done\n");
    fflush(stdout);

    FILE *fp = fopen("output.txt", "wb");
    if (fp == NULL) {
        printf("file open error\n");
        fflush(stdout);
        return 1;
    }

    int total_recv = 0;
    time_t start_time = time(NULL);

    while (1)
    {
        int n = k_recvfrom(sock, buffer);
        if (n > 0) {
            fwrite(buffer, 1, n, fp);
            fflush(fp);
            total_recv++;
            printf("user2: received message %d (%d bytes)\n", total_recv, n);
            fflush(stdout);
        } else {
            usleep(1000); // 1 ms 
            if (difftime(time(NULL), start_time) > T * 100) {
                printf("user2: time limit reached, exiting\n");
                fflush(stdout);
                break;
            }
        }
    }

    fclose(fp);
    k_close(sock);
    printf("user2: done, received %d messages\n", total_recv);
    fflush(stdout);
    return 0;
}