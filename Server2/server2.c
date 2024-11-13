#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "server2.h"
#include "client2.h"
#include "awale.h"

typedef struct {
    Plateau board;           // Awalé game board, assuming Plateau is defined in awale.h
    int player_sockets[2];   // Socket descriptors of the two players
    int current_turn;        // Indicates which player’s turn it is (0 or 1)
} GameRoom;

GameRoom game_rooms[MAX_CLIENTS];

static Client clients[MAX_CLIENTS];
static int room_counter = 0;

static void init(void) {
#ifdef WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err < 0) {
        puts("WSAStartup failed!");
        exit(EXIT_FAILURE);
    }
#endif
}

static void end(void) {
#ifdef WIN32
    WSACleanup();
#endif
}

static void send_player_list(Client *clients, int actual) {
    char buffer[BUF_SIZE] = "Connected clients:\n";
    for (int i = 0; i < actual; i++) {
        if (clients[i].in_room == 0) {
            strncat(buffer, clients[i].name, sizeof(buffer) - strlen(buffer) - 1);
            strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
        }
    }
    for (int i = 0; i < actual; i++) {
        if (clients[i].in_room == 0) {
            write_client(clients[i].sock, buffer);
        }
    }
}

static void send_welcome_message(Client *client) {
    const char *welcome_msg = "Welcome! Options:\n1. Show list of connected clients\n2. Disconnect\n3. Join Game\n";
    write_client(client->sock, welcome_msg);
}

static void handle_join_game(int client_index, int actual) {
    char buffer[BUF_SIZE] = "Available clients for a duel:\n";
    for (int i = 0; i < actual; i++) {
        if (i != client_index && clients[i].in_room == 0) {
            strncat(buffer, clients[i].name, sizeof(buffer) - strlen(buffer) - 1);
            strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
        }
    }

    // Send list to the requesting client
    write_client(clients[client_index].sock, buffer);

    // Ask client to choose an opponent by name
    const char *prompt = "Enter the name of the client you want to challenge: ";
    write_client(clients[client_index].sock, prompt);

    // Mark client as in the process of choosing an opponent
    clients[client_index].waiting_for_response = 1;
}

static void send_duel_request(int requester_index, const char *target_name, int actual) {
    char buffer[BUF_SIZE];
    int target_index = -1;

    for (int i = 0; i < actual; i++) {
        if (strcmp(clients[i].name, target_name) == 0 && clients[i].in_room == 0) {
            target_index = i;
            break;
        }
    }

    if (target_index == -1) {
        snprintf(buffer, BUF_SIZE, "Player %s is not available for a duel.\n", target_name);
        write_client(clients[requester_index].sock, buffer);
    } else {
        snprintf(buffer, BUF_SIZE, "%s has challenged you to a duel! Type 'accept' to join.", clients[requester_index].name);
        write_client(clients[target_index].sock, buffer);

        snprintf(buffer, BUF_SIZE, "Duel request sent to %s. Waiting for acceptance...\n", clients[target_index].name);
        write_client(clients[requester_index].sock, buffer);

        clients[requester_index].room_id = room_counter;
        clients[target_index].room_id = room_counter;
        room_counter++;
    }
}

static void start_private_chat(int client1, int client2) {
    clients[client1].in_room = 1;
    clients[client2].in_room = 1;
    
    int room_id = clients[client1].room_id;
    clients[client2].room_id = room_id;

    GameRoom *game_room = &game_rooms[room_id];
    game_room->player_sockets[0] = clients[client1].sock;
    game_room->player_sockets[1] = clients[client2].sock;
    game_room->current_turn = 0; // Start with player 0

    init_plateau(&game_room->board); // Initialize the Awalé board

    // Notify both clients about the game start
    char start_msg[BUF_SIZE];
    snprintf(start_msg, BUF_SIZE, "Awalé game started between %s and %s. %s goes first.\n",
             clients[client1].name, clients[client2].name, clients[client1].name);
    write_client(clients[client1].sock, start_msg);
    write_client(clients[client2].sock, start_msg);

    // Inform the first player to make a move
    write_client(game_room->player_sockets[0], "Your turn! Choose a pit (1-6):\n");
}




static void app(void) {
    SOCKET sock = init_connection();
    char buffer[BUF_SIZE];
    int actual = 0;
    int max = sock;
    fd_set rdfs;

    while (1) {
        FD_ZERO(&rdfs);
        FD_SET(STDIN_FILENO, &rdfs);
        FD_SET(sock, &rdfs);

        for (int i = 0; i < actual; i++) {
            FD_SET(clients[i].sock, &rdfs);
            if (clients[i].sock > max) max = clients[i].sock;
        }

        if (select(max + 1, &rdfs, NULL, NULL, NULL) == -1) {
            perror("select()");
            exit(errno);
        }

        if (FD_ISSET(STDIN_FILENO, &rdfs)) {
            break;
        } else if (FD_ISSET(sock, &rdfs)) {
            SOCKADDR_IN csin = {0};
            socklen_t sinsize = sizeof(csin);
            int csock = accept(sock, (SOCKADDR *)&csin, &sinsize);
            if (csock == SOCKET_ERROR) {
                perror("accept()");
                continue;
            }

            if (read_client(csock, buffer) <= 0) {
                continue;
            }

            Client c = {csock};
            strncpy(c.name, buffer, sizeof(c.name) - 1);
            c.in_room = 0;
            c.room_id = -1;
            c.waiting_for_response = 0;
            clients[actual] = c;
            actual++;

            send_welcome_message(&c);
            send_player_list(clients, actual);
        } else {
            for (int i = 0; i < actual; i++) {
                if (FD_ISSET(clients[i].sock, &rdfs)) {
                    int n = read_client(clients[i].sock, buffer);
                    if (n <= 0) {
                        close(clients[i].sock);
                        remove_client(clients, i, &actual);
                        send_player_list(clients, actual);
                    } else {
                        buffer[n] = '\0';
                        if (clients[i].waiting_for_response) {
                            char *target_name = buffer;
                            target_name[strcspn(target_name, "\n")] = '\0';
                           send_duel_request(i, target_name, actual);
                        } else if (strcmp(buffer, "1") == 0) {
                            send_player_list(clients, actual);
                        } else if ((strcmp(buffer, "2") == 0) && !clients[i].in_room) {
                            write_client(clients[i].sock, "Disconnecting...\n");
                            close(clients[i].sock);
                            remove_client(clients, i, &actual);
                            send_player_list(clients, actual);
                        } else if (strcmp(buffer, "3") == 0) {
                            handle_join_game(i, actual);
                        } else if (strcmp(buffer, "accept") == 0) {
                            for (int j = 0; j < actual; j++) {
                                if (clients[j].room_id == clients[i].room_id && i != j && clients[j].in_room == 0) {
                                    start_private_chat(i, j);
                                    break;
                                }
                            }
                        } else if (strcmp(buffer, "exit") == 0) {
                            clients[i].in_room = 0;
                            clients[i].room_id = -1;
                            send_welcome_message(&clients[i]);
                            send_player_list(clients, actual);
                        }else if (clients[i].in_room) {
                           int room_id = clients[i].room_id;
                           GameRoom *game_room = &game_rooms[room_id];
                           
                           // Ensure it's the correct player’s turn
                           if (clients[i].sock == game_room->player_sockets[game_room->current_turn]) {
                              int move = atoi(buffer);  // Assuming client sends the pit number as text
                              int result = jouer_coup(&game_room->board, game_room->current_turn, move - 1);
                              
                              // Send updated board state to both players
                              char board_state[BUF_SIZE];
                              const char *board_str = afficher_plateau(&game_room->board);
                              send_to_room(room_id, board_str);
                              for (int j = 0; j < 2; j++) {
                                    write_client(game_room->player_sockets[j], board_state);
                              }

                              // Check for game end or switch turns
                              if (result) {
                                    snprintf(board_state, BUF_SIZE, "Player %s wins!\n", clients[i].name);
                                    write_client(game_room->player_sockets[0], board_state);
                                    write_client(game_room->player_sockets[1], board_state);

                                    // Reset room status
                                    clients[i].in_room = 0;
                                    clients[i == 0 ? 1 : 0].in_room = 0;
                              } else {
                                    game_room->current_turn = 1 - game_room->current_turn;
                                    write_client(game_room->player_sockets[game_room->current_turn], "Your turn! Choose a pit (1-6):\n");
                              }
                           } else {
                              write_client(clients[i].sock, "Not your turn. Wait for the other player.\n");
                           }
                        }

                    }
                }
            }
        }
    }

    clear_clients(clients, actual);
    end_connection(sock);
}


static void clear_clients(Client *clients, int actual) {
    for (int i = 0; i < actual; i++) {
        close(clients[i].sock);
    }
}

static void remove_client(Client *clients, int to_remove, int *actual) {
    close(clients[to_remove].sock);
    memmove(clients + to_remove, clients + to_remove + 1, (*actual - to_remove - 1) * sizeof(Client));
    (*actual)--;
}

int init_connection(void) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    SOCKADDR_IN sin = {0};
    if (sock == INVALID_SOCKET) {
        perror("socket()");
        exit(errno);
    }

    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(PORT);
    sin.sin_family = AF_INET;

    if (bind(sock, (SOCKADDR *)&sin, sizeof(sin)) == SOCKET_ERROR) {
        perror("bind()");
        exit(errno);
    }

    if (listen(sock, MAX_CLIENTS) == SOCKET_ERROR) {
        perror("listen()");
        exit(errno);
    }

    return sock;
}

void send_to_room(int room_id, const char *buffer) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_room && clients[i].room_id == room_id) {
            write_client(clients[i].sock, buffer);
        }
    }
}

void end_connection(int sock) {
    close(sock);
}

int read_client(SOCKET sock, char *buffer) {
    int n = recv(sock, buffer, BUF_SIZE - 1, 0);
    if (n < 0) {
        perror("recv()");
    }
    buffer[n] = '\0';
    return n;
}



void write_client(SOCKET sock,const char *buffer) {
    if (send(sock, buffer, strlen(buffer), 0) < 0) {
        perror("send()");
    }
}




int main(int argc, char **argv) {
    init();
    app();
    end();
    return EXIT_SUCCESS;
}
