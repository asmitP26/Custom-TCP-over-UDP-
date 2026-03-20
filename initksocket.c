#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include "ksocket.h"
sharedmemory *SM;

void *thread_R(void *arg)
{
  while (1)
  {
    fd_set readfds;    // readfd store all UDP sockets we want to monitor
    FD_ZERO(&readfds); // clear the set

    // so initially
    // readfds = empty

    //  find all active UDP sockets
    int maxfd = -1;

    // Add all UDP sockets to fd_Set
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
      if (SM->sockets[i].is_free == 0) // means occupied
      {
        int udp_fd = SM->sockets[i].udp_socket; // get the udp socket used by the ktp socket

        FD_SET(udp_fd, &readfds); // add that to the monitoring list

        if (udp_fd > maxfd) // this is to keep track of the largest socket discriptor
          maxfd = udp_fd;
      }
    }

    // this is to set timeout for the select
    struct timeval tv;
    tv.tv_sec = 1;  // select will wait maximum 1 sec
    tv.tv_usec = 0; // if no packet arrive time out occur

    int activity = select(maxfd + 1, &readfds, NULL, NULL, &tv); // here we wait for the incoming packets

    if (activity == 0)
    {
      for (int i = 0; i < MAX_SOCKETS; i++)
      {
        if (SM->sockets[i].is_free == 0)
        {
          if (SM->sockets[i].nospace == 1 && SM->sockets[i].rwnd.size > 0) // Receiver buffer was full before
          // this situation happens when the application called k_recvfrom() and removed some messages.
          // but now space is available.
          {
            KTP_Message dup_ack;

            dup_ack.type = ACK;
            dup_ack.seq_no = SM->sockets[i].swnd.next_seq_no - 1;
            dup_ack.rwnd = SM->sockets[i].rwnd.size;

            sendto(SM->sockets[i].udp_socket,
                   &dup_ack,
                   sizeof(dup_ack),
                   0,
                   (struct sockaddr *)&SM->sockets[i].dest_addr,
                   sizeof(SM->sockets[i].dest_addr));

            SM->sockets[i].nospace = 0;
          }
        }
      }

      continue;
    }

    // here to check which socket receive data
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
      if (SM->sockets[i].is_free == 0)
      {
        int udp_fd = SM->sockets[i].udp_socket;

        if (FD_ISSET(udp_fd, &readfds)) // select() detected data on this socket.
        {
          KTP_Message msg;
          struct sockaddr_in sender;
          socklen_t addrlen = sizeof(sender);

          recvfrom(udp_fd, &msg, sizeof(msg), 0, (struct sockaddr *)&sender, &addrlen); // receive packet
        

          if (dropmessage(P) == 1) // for packet loss (if this return 1 then ignore this packet)
            continue;

          // DATA message
          if (msg.type == DATA) // means sender send Data not ACK
          {
            int index = -1;

            for (int j = 0; j < BUFFER_SIZE; j++) // find free slot in receiver buffer
            {
              if (SM->sockets[i].recv_buffer[j][0] == '\0')
              {
                index = j;
                break;
              }
            }

            if (index != -1)
            {
              // Copy message data into receive buffer.
              memcpy(SM->sockets[i].recv_buffer[index], msg.data, MESSAGE_SIZE);

              SM->sockets[i].rwnd.size--; // One buffer slot is now occupied
                                          //  Free space decreases.

              if (SM->sockets[i].rwnd.size == 0)
                SM->sockets[i].nospace = 1; // if no space left in the receiver buffer
            }

            // send ACK
            KTP_Message ack;
            ack.type = ACK;
            ack.seq_no = msg.seq_no;
            ack.rwnd = SM->sockets[i].rwnd.size; // tell sender how much space receiver has

            sendto(udp_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&sender, addrlen); // send acknowledgement to sender
          }

          // ACK message
          else if (msg.type == ACK)
          {
            int found = 0;

            for (int j = 0; j < BUFFER_SIZE; j++)
            {
              if (SM->sockets[i].swnd.seq_numbers[j] == msg.seq_no)
              {
                SM->sockets[i].send_buffer[j][0] = '\0';
                found = 1;
                break;
              }
            }

            // duplicate ACK case
            if (!found)
            {
              SM->sockets[i].swnd.size = msg.rwnd;
            }
            else
            {
              SM->sockets[i].swnd.size = msg.rwnd;
            }
          }
        }
      }
    }
  }
}

void *thread_S(void *arg)
{
  while (1)
  {
    // sleep for T/2
    struct timespec ts;
    ts.tv_sec = T / 2;
    ts.tv_nsec = 0;

    nanosleep(&ts, NULL); // use nanosleep as said in the pdf 

    double current_time = time(NULL); // calculate the current time

    // check every KTP socket
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
      if (SM->sockets[i].is_free == 0)
      {
        int udp_fd = SM->sockets[i].udp_socket;

        // check timeout for messages in swnd
        for (int j = 0; j < BUFFER_SIZE; j++)
        {
          if (SM->sockets[i].send_buffer[j][0] != '\0')
          {
            double time_diff = current_time - SM->sockets[i].swnd.send_time[j];

            if (time_diff >= T)
            {
              // retransmit message

              KTP_Message msg;

              msg.type = DATA;
              msg.seq_no = SM->sockets[i].swnd.seq_numbers[j];
              msg.rwnd = SM->sockets[i].rwnd.size;

              memcpy(msg.data, SM->sockets[i].send_buffer[j], MESSAGE_SIZE);
              sendto(udp_fd, &msg, sizeof(msg), 0, (struct sockaddr *)&SM->sockets[i].dest_addr, sizeof(SM->sockets[i].dest_addr));

              //  after sending update send time as new send time
              SM->sockets[i].swnd.send_time[j] = current_time;
            }
          }
        }

        // send new pending msg
        for (int j = 0; j < BUFFER_SIZE; j++)
        {
          if (SM->sockets[i].send_buffer[j][0] != '\0')
          {
            // check if message not yet sent
            if (SM->sockets[i].swnd.send_time[j] == 0)
            {
              KTP_Message msg;

              msg.type = DATA;
              msg.seq_no = SM->sockets[i].swnd.seq_numbers[j];
              msg.rwnd = SM->sockets[i].rwnd.size;

              memcpy(msg.data, SM->sockets[i].send_buffer[j], MESSAGE_SIZE);

              sendto(udp_fd, &msg, sizeof(msg), 0, (struct sockaddr *)&SM->sockets[i].dest_addr, sizeof(SM->sockets[i].dest_addr));

              /* record send timestamp */
              SM->sockets[i].swnd.send_time[j] = current_time;
            }
          }
        }
      }
    }
  }
}
void garbage_collector()
{
  while (1)
  {
    sleep(5);

    for (int i = 0; i < MAX_SOCKETS; i++)
    {
      if (SM->sockets[i].is_free == 0)
      {
        pid_t pid = SM->sockets[i].pid;

        if (kill(pid, 0) == -1 && errno == ESRCH)
        {
          // process no longer exists

          close(SM->sockets[i].udp_socket);

          for (int j = 0; j < BUFFER_SIZE; j++)
          {
            memset(SM->sockets[i].send_buffer[j], 0, MESSAGE_SIZE);
            memset(SM->sockets[i].recv_buffer[j], 0, MESSAGE_SIZE);
          }

          SM->sockets[i].is_free = 1;
          SM->sockets[i].pid = -1;
        }
      }
    }
  }
}
int main()
{
  key_t key = ftok("ksocket.h", 65);
  int shmid = shmget(key, sizeof(sharedmemory), IPC_CREAT | 0666);

  if (shmid < 0)
  {
    perror("shmget failed");
    exit(1);
  }

  SM = (sharedmemory *)shmat(shmid, NULL, 0);

  if (SM == (void *)-1)
  {
    perror("shmat failed");
    exit(1);
  }

  // initialize socket table
  for (int i = 0; i < MAX_SOCKETS; i++)
  {
    SM->sockets[i].is_free = 1;
    SM->sockets[i].pid = -1;
    SM->sockets[i].nospace = 0;
    SM->sockets[i].rwnd.size = BUFFER_SIZE;
    SM->sockets[i].swnd.next_seq_no = 1;

    for (int j = 0; j < BUFFER_SIZE; j++)
    {
      memset(SM->sockets[i].send_buffer[j], 0, MESSAGE_SIZE);
      memset(SM->sockets[i].recv_buffer[j], 0, MESSAGE_SIZE);

      SM->sockets[i].swnd.seq_numbers[j] = 0;
      SM->sockets[i].swnd.send_time[j] = 0;
    }
  }

  pthread_t r_thread, s_thread;

  // start thread R
  pthread_create(&r_thread, NULL, thread_R, NULL);

  // start thread S
  pthread_create(&s_thread, NULL, thread_S, NULL);

  // start garbage collector process
  pid_t gc = fork();

  if (gc == 0)
  {
    garbage_collector();
    exit(0);
  }

  // keep init process alive
  while (1)
    sleep(1000);

  return 0;
}