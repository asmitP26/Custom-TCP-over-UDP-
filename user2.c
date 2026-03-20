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
           "127.0.0.1", 6000,
           "127.0.0.1", 5000);

    fp = fopen("output.txt", "wb");

    if (fp == NULL)
    {
        printf("file open error\n");
        return 0;
    }

    while (1)
    {
        int n = k_recvfrom(sock, buffer);

        if (n > 0)
        {
            fwrite(buffer, 1, n, fp);
        }
    }

    fclose(fp);

    k_close(sock);

    return 0;
}