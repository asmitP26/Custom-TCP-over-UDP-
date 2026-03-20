#include "ksocket.h"
#include <errno.h>
#include <string.h>

sharedmemory *SM;

int dropmessage(float p)
{
  double random = (double)rand() / RAND_MAX;
  if (random < p)
  {
    return 1;
  }
  else
    return 0;
}

int k_socket(int domain, int type, int protocol)
{
  key_t key = ftok("ksocket.h", 65);
  int shmid = shmget(key, sizeof(sharedmemory), 0666);
  SM = (sharedmemory *)shmat(shmid, NULL, 0);
  if (type != SOCK_KTP)
  {
    return -1;
  }
  for (int i = 0; i < MAX_SOCKETS; i++)
  {
    if (SM->sockets[i].is_free == 1)
    {

      SM->sockets[i].is_free = 0;
      SM->sockets[i].pid = getpid();
      SM->sockets[i].udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
      if (SM->sockets[i].udp_socket < 0)
      {
        return -1;
      }
      SM->sockets[i].swnd.next_seq_no = 1;
      SM->sockets[i].swnd.size = 0;
      SM->sockets[i].nospace = 0;

      return i;
    }
  }
  return -1;
}

int k_bind(int sockfd, char *src_ip, int src_port, char *dest_ip, int dest_port)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
  {
    printf("Invalid socket file descriptor\n");
    return -1;
  }
  // cunstruct source address
  struct sockaddr_in src_addr, dest_addr;
  memset(&src_addr, 0, sizeof(src_addr));
  src_addr.sin_family = AF_INET;
  src_addr.sin_port = htons(src_port);
  inet_pton(AF_INET, src_ip, &src_addr.sin_addr);

  // bind UDP socket
  if (bind(SM->sockets[sockfd].udp_socket,
           (struct sockaddr *)&src_addr,
           sizeof(src_addr)) < 0)
  {
    return -1;
  }

  // store source address in shared memory
  SM->sockets[sockfd].src_addr = src_addr;

  // construct destination address
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(dest_port);
  inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr);

  // store the destination address
  SM->sockets[sockfd].dest_addr = dest_addr;
  return 0;
}
#include <errno.h>
#include <string.h>

int k_sendto(int sockfd, char *msg, int len)
{
  printf("Message inserted in send buffer\n");
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return -1;

  // check if socket is bound
  if (SM->sockets[sockfd].dest_addr.sin_port == 0)
  {
    errno = ENOTBOUND;
    return -1;
  }
  // check if send buffer has space
  int index = -1;
  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    if (SM->sockets[sockfd].send_buffer[i][0] == '\0')
    {
      index = i;
      break;
    }
  }

  if (index == -1)
  {
    errno = ENOMESSAGE;
    return -1;
  }

  // copy message into send buffer
  strncpy(SM->sockets[sockfd].send_buffer[index], msg, MESSAGE_SIZE);

  // update sending window sequence numbers
  SM->sockets[sockfd].swnd.seq_numbers[index] =
      SM->sockets[sockfd].swnd.next_seq_no;

  // increment the sequence number
  SM->sockets[sockfd].swnd.next_seq_no++;

  // record send time
  SM->sockets[sockfd].swnd.send_time[index] = time(NULL);

  return len;
}
int k_recvfrom(int sockfd, char *buffer)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return -1;

  // check if receive buffer has is empty (if empty then give errro )
  int index = -1;
  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    if (SM->sockets[sockfd].recv_buffer[i][0] != '\0')
    {
      index = i;
      break;
    }
  }

  if (index == -1)
  {
    errno = ENOSPACE;
    return -1;
  }

  // copy message into receive buffer
  strncpy(buffer, SM->sockets[sockfd].recv_buffer[index], MESSAGE_SIZE);

  // remove msg from recive buffer
  memset(SM->sockets[sockfd].recv_buffer[index], 0, MESSAGE_SIZE);

  // update the sliding window
  SM->sockets[sockfd].rwnd.size++;

  // return message_size
  return MESSAGE_SIZE;
}

int k_close(int sockfd)
{
  if (sockfd < 0 || sockfd >= MAX_SOCKETS)
    return -1;

  // close the socket
  close(SM->sockets[sockfd].udp_socket);

  // clear the send buffer
  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    memset(SM->sockets[sockfd].send_buffer[i], 0, MESSAGE_SIZE);
  }
  // clear the recieve buffer
  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    memset(SM->sockets[sockfd].recv_buffer[i], 0, MESSAGE_SIZE);
  }

  // reset the window variables
  SM->sockets[sockfd].swnd.next_seq_no = 1;
  SM->sockets[sockfd].rwnd.size = BUFFER_SIZE;
  SM->sockets[sockfd].nospace = 0;

  // mark the socket entry as free
  SM->sockets[sockfd].is_free = 1;

  return 0;
}