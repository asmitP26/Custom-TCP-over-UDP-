#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ksocket.h"

int main()
{
    int sock;
    char buffer[MESSAGE_SIZE];

    sock = k_socket(AF_INET, SOCK_KTP, 0);
    if (sock < 0)
    {
        printf("socket creation failed\n");
        fflush(stdout);
        return 1;
    }
    printf("user1: socket created = %d\n", sock);
    fflush(stdout);

    if (k_bind(sock, "127.0.0.1", 5000, "127.0.0.1", 6000) < 0)
    {
        printf("k_bind failed\n");
        fflush(stdout);
        return 1;
    }
    printf("user1: bind done\n");
    fflush(stdout);

    FILE *fp = fopen("input.txt", "rb");
    if (fp == NULL)
    {
        printf("file open error\n");
        fflush(stdout);
        return 1;
    }

    int total_sent = 0;
    int total_messages = 0;
    while (1)
    {
        int n = fread(buffer, 1, MESSAGE_SIZE, fp);
        if (n <= 0)
            break;

        while (k_sendto(sock, buffer, n) < 0)
        {
            usleep(10000);
        }
        total_sent++;
        total_messages++;
        printf("user1: queued message %d (%d bytes)\n", total_sent, n);
        fflush(stdout);
    }

    fclose(fp);
    printf("user1: all %d messages queued, waiting for delivery...\n", total_sent);
    fflush(stdout);

    // Give thread_S plenty of time to drain the send buffer 
    sleep(T * 60);
    long total_tx = k_get_total_transmissions();
    printf("Total messages generated = %d\n", total_messages);
    printf("Total transmissions made = %ld\n", total_tx);
    printf("Average transmissions per message = %.2f\n", (float)total_tx / total_messages);
    fflush(stdout);
    fflush(stdout);
    k_close(sock);
    printf("user1: done\n");
    fflush(stdout);
    return 0;
}