#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "server2.h"
#include "client2.h"
#include "awale.h"

#define MAX_BIO_LENGTH 256


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
    const char *welcome_msg = 
        "Welcome! Options:\n"
        "1. Show list of connected clients\n"
        "2. Disconnect\n"
        "3. Join Game\n"
        "4. Set/Update Bio\n"
        "5. View Player Bio\n";
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
    clients[client_index].in_room = 0;
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
    clients[client1].waiting_for_response = 0;
    clients[client2].waiting_for_response = 0;
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

int fetch_bio(const char *name, char *bio, size_t bio_size) {
    FILE *file = fopen("bios.txt", "r");
    if (!file) {
        perror("Failed to open bios file for reading");
        return 0; // Indicate failure
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *saved_name = strtok(line, "|");
        char *saved_bio = strtok(NULL, "\n");

        if (saved_name && saved_bio && strcmp(saved_name, name) == 0) {
            strncpy(bio, saved_bio, bio_size - 1);
            bio[bio_size - 1] = '\0'; // Ensure null-terminated string
            fclose(file);
            return 1; // Indicate success
        }
    }

    fclose(file);
    return 0; // Indicate failure (bio not found)
}

void set_bio(const char *name, const char *new_bio) {
    FILE *file = fopen("bios.txt", "r+");
    if (!file) {
        file = fopen("bios.txt", "w");
        if (!file) {
            perror("Failed to open bios file for writing");
            return;
        }
    }

    char line[512];
    char temp_filename[] = "bios_tmp.txt";
    FILE *temp_file = fopen(temp_filename, "w");
    if (!temp_file) {
        perror("Failed to create temporary file");
        fclose(file);
        return;
    }

    int updated = 0;

    // Copy lines, updating the bio if the name matches
    while (fgets(line, sizeof(line), file)) {
        char *saved_name = strtok(line, "|");
        char *saved_bio = strtok(NULL, "\n");

        if (saved_name && strcmp(saved_name, name) == 0) {
            fprintf(temp_file, "%s|%s\n", name, new_bio);
            updated = 1;
        } else {
            fprintf(temp_file, "%s|%s\n", saved_name, saved_bio);
        }
    }

    // Add new entry if name wasn't found
    if (!updated) {
        fprintf(temp_file, "%s|%s\n", name, new_bio);
    }

    fclose(file);
    fclose(temp_file);

    // Replace original file with updated file
    remove("bios.txt");
    rename(temp_filename, "bios.txt");
}

static void handle_set_bio(int client_index) {
    const char *prompt = "Enter your bio (max 10 lines, ASCII only):\n";
    write_client(clients[client_index].sock, prompt);

    char buffer[MAX_BIO_LENGTH];
    int n = read_client(clients[client_index].sock, buffer);
    if (n > 0) {
        buffer[n] = '\0';

        // Check ASCII and line constraints
        int line_count = 0;
        for (int i = 0; i < n; i++) {
            if (!isascii(buffer[i])) {
                write_client(clients[client_index].sock, "Bio must contain ASCII characters only.\n");
                return;
            }
            if (buffer[i] == '\n') line_count++;
        }
        if (line_count > 10) {
            write_client(clients[client_index].sock, "Bio must not exceed 10 lines.\n");
            return;
        }

        // Save or update the bio in the file
        set_bio(clients[client_index].name, buffer);

        write_client(clients[client_index].sock, "Bio updated successfully.\n");
    } else {
        write_client(clients[client_index].sock, "Failed to update bio. Try again.\n");
    }
}

static void handle_view_bio(int client_index, int actual) {
    const char *prompt = "Enter the name of the player whose bio you want to view:\n";
    write_client(clients[client_index].sock, prompt);

    char buffer[32];
    int n = read_client(clients[client_index].sock, buffer);
    if (n > 0) {
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

        char bio[MAX_BIO_LENGTH];
        if (fetch_bio(buffer, bio, sizeof(bio))) {
            char bio_msg[512];
            snprintf(bio_msg, sizeof(bio_msg), "Bio of %s:\n%s\n", buffer, bio);
            write_client(clients[client_index].sock, bio_msg);
        } else {
            write_client(clients[client_index].sock, "Player not found or bio not set.\n");
        }
    } else {
        write_client(clients[client_index].sock, "Failed to read input. Try again.\n");
    }
}

static void handle_new_connection(SOCKET sock, int *actual) {
    SOCKADDR_IN csin = {0};
    socklen_t sinsize = sizeof(csin);
    int csock = accept(sock, (SOCKADDR *)&csin, &sinsize);
    if (csock == SOCKET_ERROR) {
        perror("accept()");
        return;
    }

    char buffer[BUF_SIZE];
    if (read_client(csock, buffer) <= 0) {
        return;
    }

    Client c = {csock};
    strncpy(c.name, buffer, sizeof(c.name) - 1);
    c.in_room = 0;
    c.room_id = -1;
    c.waiting_for_response = 0;
    clients[*actual] = c;
    (*actual)++;

    send_welcome_message(&c);
    send_player_list(clients, *actual);
}


static void handle_disconnection(int client_index, int *actual) {
    close(clients[client_index].sock);
    remove_client(clients, client_index, actual);
    send_player_list(clients, *actual);
}

static void handle_outside_room(int client_index, char *buffer, int actual) {
    if (strcmp(buffer, "1") == 0) {
        send_player_list(clients, actual);
    } else if (strcmp(buffer, "2") == 0) {
        write_client(clients[client_index].sock, "Disconnecting...\n");
        handle_disconnection(client_index, &actual);
    } else if (strcmp(buffer, "3") == 0) {
        handle_join_game(client_index, actual);
    } else if (clients[client_index].waiting_for_response) {
        char *target_name = buffer;
        target_name[strcspn(target_name, "\n")] = '\0';
        send_duel_request(client_index, target_name, actual);
    } else if (strcmp(buffer, "accept") == 0) {
        for (int j = 0; j < actual; j++) {
            if (clients[j].room_id == clients[client_index].room_id && j != client_index && clients[j].in_room == 0) {
                start_private_chat(client_index, j);
                break;
            }
        }
    }
}

static void handle_in_room(int client_index, char *buffer) {
    int room_id = clients[client_index].room_id;
    GameRoom *game_room = &game_rooms[room_id];

    if (clients[client_index].sock == game_room->player_sockets[game_room->current_turn]) {
        int move = atoi(buffer);  // Assuming client sends the pit number as text
        if (move < 1 || move > 6) {
            write_client(clients[client_index].sock, "Invalid move. Choose a pit (1-6).\n");
            return;
        }

        int result = jouer_coup(&game_room->board, game_room->current_turn, move - 1);  // Apply move
        char *board_state = afficher_plateau(&game_room->board);  // Get board state
        send_to_room(room_id, board_state);
        free(board_state);  // Free dynamically allocated memory

        if (result) {
            char end_msg[BUF_SIZE];
            snprintf(end_msg, BUF_SIZE, "Player %s wins!\n", clients[client_index].name);
            send_to_room(room_id, end_msg);
        } else {
            game_room->current_turn = 1 - game_room->current_turn;
            write_client(game_room->player_sockets[game_room->current_turn], "Your turn! Choose a pit (1-6):\n");
        }
    } else {
        write_client(clients[client_index].sock, "Not your turn. Wait for the other player.\n");
    }
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
            handle_new_connection(sock, &actual);
        } else {
            for (int i = 0; i < actual; i++) {
                if (FD_ISSET(clients[i].sock, &rdfs)) {
                    int n = read_client(clients[i].sock, buffer);
                    if (n <= 0) {
                        handle_disconnection(i, &actual);
                    } else {
                        buffer[n] = '\0';
                        if (clients[i].in_room) {
                            handle_in_room(i, buffer);
                        } else {
                            handle_outside_room(i, buffer, actual);
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
