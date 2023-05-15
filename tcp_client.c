#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

#define MAX_TOPICS 100

void erro(char *msg)
{
  printf("Erro: %s\n", msg);
  exit(-1);
}
typedef struct Topics
{
  struct sockaddr_in addr;
  socklen_t slen;
  int multicast_socket;
} Topics;

int fd, role = 0;
pthread_t multicast_thread_t;
Topics topics[MAX_TOPICS];
int topics_count = 0;
int max_fd = 0;

void ctrlc_handler()
{
  char msg[1024];
  sprintf(msg, "LEAVE");
  write(fd, msg, 1024);
  if (read(fd, msg, 1024) < -1)
  {
    printf("Error reading from server\n");
  };
  pthread_cancel(multicast_thread_t);
  pthread_join(multicast_thread_t, NULL);
  if (!strcmp(msg, "OK"))
  {
    printf("\nConnection closed\n");
    if (close(fd) < 0)
    {
      printf("\nError closing connection\n");
    }
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.0.0.1");
    mreq.imr_interface.s_addr = INADDR_ANY;
    for (int i = 0; i < topics_count; i++)
    {
      // leave the multicast group
      if (setsockopt(topics[i].multicast_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
      {
        perror("setsockopt");
      }
      close(topics[i].multicast_socket);
    }
    exit(0);
  }
  else
  {
    printf("\nError closing connection\n");
    close(fd);
    exit(1);
  }
}

void join_socket(int port)
{

  if ((topics[topics_count].multicast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("socket");
    exit(1);
  }

  // set up the multicast address structure
  memset(&topics[topics_count].addr, 0, sizeof(topics[topics_count].addr));
  topics[topics_count].addr.sin_family = AF_INET;
  topics[topics_count].addr.sin_addr.s_addr = htonl(INADDR_ANY);
  topics[topics_count].addr.sin_port = htons(port);

  // set the SO_REUSEADDR option
  int optval = 1;
  if (setsockopt(topics[topics_count].multicast_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
  {
    perror("setsockopt");
    exit(1);
  }

  // bind the socket to the port
  if (bind(topics[topics_count].multicast_socket, (struct sockaddr *)&topics[topics_count].addr, sizeof(topics[topics_count].addr)) < 0)
  {
    perror("multicast bind");
    exit(1);
  }

  // join the multicast group
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr = inet_addr("239.0.0.1");
  mreq.imr_interface.s_addr = INADDR_ANY;
  if (setsockopt(topics[topics_count].multicast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
  {
    perror("multicast setsockopt");
    exit(1);
  }
  if (topics[topics_count].multicast_socket > max_fd)
  {
    max_fd = topics[topics_count].multicast_socket;
  }
  topics_count++;
}

void *multicast_thread()
{
  int n;
  char msg[1024];
  fd_set rfds;
  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;

  socklen_t slen = sizeof(struct sockaddr_in);
  while (1)
  {
    memset(msg, 0, sizeof(msg));
    FD_ZERO(&rfds);
    for (int i = 0; i < topics_count; i++)
    {
      FD_SET(topics[i].multicast_socket, &rfds);
    }
    select(max_fd + 1, &rfds, NULL, NULL, &tv);
    for (int i = 0; i < topics_count; i++)
    {
      if (FD_ISSET(topics[i].multicast_socket, &rfds))
      {
        if ((n = recvfrom(topics[i].multicast_socket, msg, sizeof(msg), 0, (struct sockaddr *)&topics[i].addr, &slen)) < 0)
        {
          perror("multicast recvfrom");
        }
        printf("Received multicast message: %s\n", msg);
      }
    }
  }
  pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
  char endServer[100];
  int nread, temp;
  struct sockaddr_in addr;
  struct hostent *hostPtr;
  char user[1024];

  if (argc != 3)
  {
    printf("cliente <host> <port> \n");
    exit(-1);
  }

  // Initialize the signal handler
  struct sigaction ctrlc;
  ctrlc.sa_handler = ctrlc_handler;
  sigfillset(&ctrlc.sa_mask);
  ctrlc.sa_flags = 0;
  sigaction(SIGINT, &ctrlc, NULL);

  sigset_t mask;
  sigfillset(&mask);
  sigdelset(&mask, SIGINT);
  sigprocmask(SIG_SETMASK, &mask, NULL);

  pthread_create(&multicast_thread_t, NULL, multicast_thread, NULL);

  strcpy(endServer, argv[1]);
  if ((hostPtr = gethostbyname(endServer)) == 0)
    erro("Não consegui obter endereço");

  bzero((void *)&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ((struct in_addr *)(hostPtr->h_addr))->s_addr;
  addr.sin_port = htons((short)atoi(argv[2]));

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    erro("socket");
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    erro("Connect");

  // Create the arrays to store the response and the domain
  char msg[1024], aux[1024], cmd[1024], username[1024], password[1024];
  // Waits for the first message sent by the server
  if ((nread = read(fd, msg, 1024)) < -1)
  {
    printf("Error reading from server\n");
    ctrlc_handler();
  };
  // If a message is received the client prints the message
  if (nread > 0)
  {
    printf("%s\n", msg);
  }
  do
  {
    // Reads the command from the user
    memset(cmd, 0, 1024);
    fgets(msg, 1024, stdin);
    if (sscanf(msg, " %s", cmd) != 1 || strlen(cmd) < 2)
    {
      continue;
    }
    if (!strcmp(cmd, "LOGIN"))
    {
      if (role)
      {
        printf("You are already logged in\n");
        continue;
      }
      if (sscanf(msg, " %s %s %s", cmd, username, password) != 3)
      {
        printf("LOGIN {username} {password}\n");
        continue;
      }
      // Sends the domain to the server
      write(fd, msg, 1024);
      // Waits for the server response to the given domain
      if ((nread = read(fd, msg, 1024)) < -1)
      {
        printf("Error reading from server\n");
        ctrlc_handler();
      };
      // Prints the response from the server
      if (sscanf(msg, " %d", &role) != 1)
      {
        printf("Invalid command\n");
        continue;
      }
      else if (role == 0)
      {
        printf("Invalid username or password\n");
        continue;
      }
      else if (role == 1)
      {
        printf("Journalist logged in\n");
        continue;
      }
      else if (role == 2)
      {
        printf("Reader logged in\n");
        strcpy(user, username);
        char *token = strtok(msg, " ");
        int port = 0;
        while (token != NULL)
        {
          token = strtok(NULL, " ");
          if (token == NULL)
          {
            break;
          }
          sscanf(token, "%d", &port);
          join_socket(port);
        }
        continue;
      }
    }
    else if (!strcmp(cmd, "LIST_TOPICS") && role == 2)
    {
      // Sends the domain to the server
      write(fd, msg, 1024);
      // Waits for the server response to the given domain
      if ((nread = read(fd, msg, 1024)) < -1)
      {
        printf("Error reading from server\n");
        ctrlc_handler();
      };
      // Prints the response from the server
      printf("%s\n", msg);
    }
    else if (!strcmp(cmd, "SUBSCRIBE_TOPIC") && role == 2)
    {
      if (sscanf(msg, " %s %s", cmd, aux) != 2)
      {
        printf("SUBSCRIBE_TOPIC {topic}\n");
        continue;
      }
      sprintf(msg, "%s %s %s", cmd, aux, user);
      write(fd, msg, 1024);
      if ((nread = read(fd, msg, 1024)) < -1)
      {
        printf("Error reading from server\n");
        ctrlc_handler();
      };
      if (sscanf(msg, " %d", &temp) != 1)
      {
        printf("Invalid command\n");
        continue;
      }
      if (temp == 0)
      {
        printf("Invalid topic\n");
        continue;
      }
      else
      {
        printf("You are now subscribed in this topic\n");
        join_socket(temp);
        continue;
      }
    }
    else if (!strcmp(cmd, "SEND_NEWS") && role == 1)
    {
      if (sscanf(msg, " %s %s %s", cmd, aux, aux) != 3)
      {
        printf("SEND_NEWS {topic} {news}\n");
        continue;
      }
      write(fd, msg, 1024);
      if ((nread = read(fd, msg, 1024)) < -1)
      {
        printf("Error reading from server\n");
        ctrlc_handler();
      };
      if (sscanf(msg, " %d", &temp) != 1)
      {
        printf("Invalid command\n");
        continue;
      }
      if (temp == 0)
      {
        printf("Invalid topic\n");
        continue;
      }
      else if (temp == 1)
      {
        printf("News sent\n");
        continue;
      }
    }
    else if (!strcmp(cmd, "CREATE_TOPIC") && role == 1)
    {
      if (sscanf(msg, " %s %s %s", cmd, username, password) != 3)
      {
        printf("CREATE_TOPIC {id} {topic}\n");
        continue;
      }
      write(fd, msg, 1024);
      if ((nread = read(fd, msg, 1024)) < -1)
      {
        printf("Error reading from server\n");
        ctrlc_handler();
      };
      if (sscanf(msg, " %d", &temp) != 1)
      {
        printf("Invalid command\n");
        continue;
      }
      if (temp == 0)
      {
        printf("Failed to create topic\n");
        continue;
      }
      else if (temp == 1)
      {
        printf("Topic created\n");
        continue;
      }
    }
    else if (role == 0)
    {
      printf("You need to login first\n");
      continue;
    }
    else
    {
      printf("Invalid command\n");
      continue;
    }

  } while (1); // If the server response is the string "Ate logo!" the connection is closed
}
