#ifndef SERVER_H
#define SERVER_H

#ifdef WIN32

#include <winsock2.h>

#elif defined (linux)

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> /* close */
#include <netdb.h> /* gethostbyname */
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)
typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;

#else

#error not defined for this platform

#endif

#define CRLF        "\r\n"
#define PORT         1977
#define MAX_CLIENTS     100

#define BUF_SIZE    1024

#include "client2.h"


static void init(void);
static void end(void);
static void app(void);
static int init_connection(void);
static void end_connection(int sock);
static int read_client(SOCKET sock, char *buffer);
static void write_client(SOCKET sock, const char *buffer);
static void send_message_to_all_clients(Client *clients, Client client, int actual, const char *buffer, char from_server);
static void remove_client(Client *clients, int to_remove, int *actual);
static void clear_clients(Client *clients, int actual);
static void send_to_room(int room_id, const char *buffer);
int fetch_bio(const char *name, char *bio, size_t bio_size);
void set_bio(const char *name, const char *new_bio);
static void handle_set_bio(int client_index);
static void handle_view_bio(int client_index, int actual);
static void handle_new_connection(SOCKET sock, int *actual);
static void handle_disconnection(int client_index, int *actual);
static void observe_game(int client_index, int room_id);
static void list_ongoing_games(int client_index);
static void notify_observers(int room_id, const char *message);
static void send_player_list(Client *clients, int actual, int client_index);
static void send_welcome_message(Client *client);
static void handle_join_game(int client_index, int actual);
static void handle_outside_room(int client_index, char *buffer, int actual);
static void handle_in_room(int client_index, char *buffer);
static void add_player_to_registry(const char *name);
int player_exists(const char *name);
static void add_observer(int room_id, int observer_socket, int c);
int are_friends(const char *name1, const char *name2);
void send_friend_request(const char *sender, const char *receiver);
int friend_request_exists(const char *sender, const char *receiver);

#endif /* guard */
