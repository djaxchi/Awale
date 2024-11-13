#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "client.h"
#include "awale.h"

typedef struct {
    Client client;
    int in_game; // 0 if waiting, 1 if in-game
} Player;

static Player players[MAX_CLIENTS];
static Plateau plateau;

static void init(void) {
#ifdef WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if(err < 0) {
        puts("WSAStartup failed !");
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
    char buffer[BUF_SIZE] = "Current players:\n";
    for (int i = 0; i < actual; i++) {
        if (!players[i].in_game) {
            strncat(buffer, players[i].client.name, sizeof(buffer) - strlen(buffer) - 1);
            strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
        }
    }
    for (int i = 0; i < actual; i++) {
        write_client(clients[i].sock, buffer);
    }
}

static void start_game(int player1, int player2) {
    players[player1].in_game = 1;
    players[player2].in_game = 1;
    init_plateau(&plateau);

    char start_msg[BUF_SIZE];
    snprintf(start_msg, BUF_SIZE, "Game started between %s and %s!", players[player1].client.name, players[player2].client.name);
    
    write_client(players[player1].client.sock, start_msg);
    write_client(players[player2].client.sock, start_msg);
}

static void handle_request(int requester, int responder) {
    char buffer[BUF_SIZE];
    snprintf(buffer, BUF_SIZE, "%s wants to play with you. Type 'accept' to start the game.", players[requester].client.name);
    write_client(players[responder].client.sock, buffer);
}

static void app(void) {
    SOCKET sock = init_connection();
    char buffer[BUF_SIZE];
    int actual = 0;
    int max = sock;
    fd_set rdfs;

    while (1) {
        int i;
        FD_ZERO(&rdfs);
        FD_SET(STDIN_FILENO, &rdfs);
        FD_SET(sock, &rdfs);

        for (i = 0; i < actual; i++) {
            FD_SET(players[i].client.sock, &rdfs);
        }

        if (select(max + 1, &rdfs, NULL, NULL, NULL) == -1) {
            perror("select()");
            exit(errno);
        }

        if (FD_ISSET(STDIN_FILENO, &rdfs)) {
            break;
        } else if (FD_ISSET(sock, &rdfs)) {
            SOCKADDR_IN csin = {0};
            size_t sinsize = sizeof csin;
            int csock = accept(sock, (SOCKADDR *)&csin, (socklen_t *)&sinsize);
            if (csock == SOCKET_ERROR) {
                perror("accept()");
                continue;
            }

            if (read_client(csock, buffer) == -1) {
                continue;
            }

            max = csock > max ? csock : max;
            FD_SET(csock, &rdfs);

            Client c = {csock};
            strncpy(c.name, buffer, BUF_SIZE - 1);
            players[actual].client = c;
            players[actual].in_game = 0;
            actual++;

            send_player_list(players, actual); // Broadcast updated player list to all clients
        } else {
            for (i = 0; i < actual; i++) {
                if (FD_ISSET(players[i].client.sock, &rdfs)) {
                    Client client = players[i].client;
                    int c = read_client(client.sock, buffer);

                    if (c == 0) {
                        closesocket(client.sock);
                        remove_client(players, i, &actual);
                        send_player_list(players, actual);
                    } else {
                        buffer[c] = '\0';
                        if (strncmp(buffer, "list", 4) == 0) {
                            send_player_list(players, actual);
                        } else if (strncmp(buffer, "play ", 5) == 0) {
                            char *target_name = buffer + 5;
                            target_name[strcspn(target_name, "\n")] = '\0';
                            int target_index = -1;
                            for (int j = 0; j < actual; j++) {
                                if (strcmp(players[j].client.name, target_name) == 0 && !players[j].in_game) {
                                    target_index = j;
                                    break;
                                }
                            }
                            if (target_index != -1) {
                                handle_request(i, target_index);
                            } else {
                                write_client(client.sock, "Player not available.\n");
                            }
                        } else if (strncmp(buffer, "accept", 6) == 0) {
                            for (int j = 0; j < actual; j++) {
                                if (players[j].in_game == 0 && i != j) {
                                    start_game(i, j);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    clear_clients(players, actual);
    end_connection(sock);
}


int main(int argc, char **argv) {
    init();
    app();
    end();
    return EXIT_SUCCESS;
}
