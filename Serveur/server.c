#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "server.h"
#include "client.h"
#include "../awale.h"

static Plateau plateau;

static void init(void)
{
#ifdef WIN32
   WSADATA wsa;
   int err = WSAStartup(MAKEWORD(2, 2), &wsa);
   if(err < 0)
   {
      puts("WSAStartup failed !");
      exit(EXIT_FAILURE);
   }
#endif
}

static void end(void)
{
#ifdef WIN32
   WSACleanup();
#endif
}

static void app(void)
{
   SOCKET sock = init_connection();
   char buffer[BUF_SIZE];
   int actual = 0;
   int max = sock;
   Client clients[MAX_CLIENTS];

   fd_set rdfs;

   init_plateau(&plateau);  // Initialize the Awalé board at the start of the game

   while(1)
   {
      int i = 0;
      FD_ZERO(&rdfs);

      FD_SET(STDIN_FILENO, &rdfs);
      FD_SET(sock, &rdfs);

      for(i = 0; i < actual; i++)
      {
         FD_SET(clients[i].sock, &rdfs);
      }

      if(select(max + 1, &rdfs, NULL, NULL, NULL) == -1)
      {
         perror("select()");
         exit(errno);
      }

      if(FD_ISSET(STDIN_FILENO, &rdfs))
      {
         break;
      }
      else if(FD_ISSET(sock, &rdfs))
      {
         SOCKADDR_IN csin = { 0 };
         size_t sinsize = sizeof csin;
         int csock = accept(sock, (SOCKADDR *)&csin, &sinsize);
         if(csock == SOCKET_ERROR)
         {
            perror("accept()");
            continue;
         }

         if(read_client(csock, buffer) == -1)
         {
            continue;
         }

         max = csock > max ? csock : max;
         FD_SET(csock, &rdfs);

         Client c = { csock };
         strncpy(c.name, buffer, BUF_SIZE - 1);
         clients[actual] = c;
         actual++;
      }
      else
      {
         for(i = 0; i < actual; i++)
         {
            if(FD_ISSET(clients[i].sock, &rdfs))
            {
               Client client = clients[i];
               int c = read_client(clients[i].sock, buffer);

               if(c == 0)
               {
                  closesocket(clients[i].sock);
                  remove_client(clients, i, &actual);
                  snprintf(buffer, BUF_SIZE, "%s disconnected!", client.name);
                  send_message_to_all_clients(clients, client, actual, buffer, 1);
               }
               else
               {
                  int case_choisie = atoi(buffer) - 1; // Convert input to 0-based index
                  if (case_choisie >= 0 && case_choisie < 6)
                  {
                      int joueur = i % 2; // Alternate turns between two players
                      if (jouer_coup(&plateau, joueur, case_choisie + (joueur == 1 ? 6 : 0)))
                      {
                          char plateau_buffer[BUF_SIZE];
                          plateau_to_string(&plateau, plateau_buffer);
                          send_message_to_all_clients(clients, client, actual, plateau_buffer, 1);
                          if (plateau.score[0] > MAX_GRAINS / 2 || plateau.score[1] > MAX_GRAINS / 2)
                          {
                              snprintf(buffer, BUF_SIZE, "Joueur %d gagne !", joueur + 1);
                              send_message_to_all_clients(clients, client, actual, buffer, 1);
                              clear_clients(clients, actual);
                              end_connection(sock);
                              return;
                          }
                      }
                      else
                      {
                          write_client(client.sock, "Coup invalide\n");
                      }
                  }
                  else
                  {
                      write_client(client.sock, "Coup invalide : Choisissez une case entre 1 et 6\n");
                  }
               }
               break;
            }
         }
      }
   }

   clear_clients(clients, actual);
   end_connection(sock);
}

static void clear_clients(Client *clients, int actual)
{
   int i = 0;
   for(i = 0; i < actual; i++)
   {
      closesocket(clients[i].sock);
   }
}

static void remove_client(Client *clients, int to_remove, int *actual)
{
   memmove(clients + to_remove, clients + to_remove + 1, (*actual - to_remove - 1) * sizeof(Client));
   (*actual)--;
}

static void send_message_to_all_clients(Client *clients, Client sender, int actual, const char *buffer, char from_server)
{
   int i = 0;
   char message[BUF_SIZE];
   message[0] = 0;
   for(i = 0; i < actual; i++)
   {
      if(sender.sock != clients[i].sock)
      {
         if(from_server == 0)
         {
            strncpy(message, sender.name, BUF_SIZE - 1);
            strncat(message, " : ", sizeof message - strlen(message) - 1);
         }
         strncat(message, buffer, sizeof message - strlen(message) - 1);
         write_client(clients[i].sock, message);
      }
   }
}

static int init_connection(void)
{
   SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
   SOCKADDR_IN sin = { 0 };

   if(sock == INVALID_SOCKET)
   {
      perror("socket()");
      exit(errno);
   }

   sin.sin_addr.s_addr = htonl(INADDR_ANY);
   sin.sin_port = htons(PORT);
   sin.sin_family = AF_INET;

   if(bind(sock,(SOCKADDR *) &sin, sizeof sin) == SOCKET_ERROR)
   {
      perror("bind()");
      exit(errno);
   }

   if(listen(sock, MAX_CLIENTS) == SOCKET_ERROR)
   {
      perror("listen()");
      exit(errno);
   }

   return sock;
}

static void end_connection(int sock)
{
   closesocket(sock);
}

static int read_client(SOCKET sock, char *buffer)
{
   int n = 0;

   if((n = recv(sock, buffer, BUF_SIZE - 1, 0)) < 0)
   {
      perror("recv()");
      n = 0;
   }

   buffer[n] = 0;

   return n;
}

static void write_client(SOCKET sock, const char *buffer)
{
   if(send(sock, buffer, strlen(buffer), 0) < 0)
   {
      perror("send()");
      exit(errno);
   }
}

int main(int argc, char **argv)
{
   init();

   app();

   end();

   return EXIT_SUCCESS;
}
