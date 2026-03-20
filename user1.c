#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ksocket.h"

int main()
{
    int sock;
    char buffer[MESSAGE_SIZE];

    FILE *fp;

    sock = k_socket(AF_INET, SOCK_KTP, 0);

    if (sock < 0)
    {
        printf("socket creation failed\n");
        return 0;
    }

    k_bind(sock,
           "127.0.0.1", 5000,
           "127.0.0.1", 6000);

    fp = fopen("input.txt", "rb");

    if (fp == NULL)
    {
        printf("file open error\n");
        return 0;
    }

    while (1)
    {
        int n = fread(buffer, 1, MESSAGE_SIZE, fp);

        if (n <= 0)
            break;

        while (k_sendto(sock, buffer, n) < 0)
            ;
    }

    fclose(fp);

    k_close(sock);

    return 0;
}