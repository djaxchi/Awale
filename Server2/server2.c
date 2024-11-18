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
    Plateau board;           // Awalé game board
    int player_sockets[2];   // Socket descriptors of the two players
    int current_turn;        // Indicates which player’s turn it is (0 or 1)
    int player_index[2];     // Index of the players in the clients array
    int observers[MAX_CLIENTS]; // Socket descriptors of observers
    int observer_count;      // Number of observers
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
        "5. View Player Bio\n"
        "6. List ongoing games\n"
        "To observe a game, type 'observe <room_id>'\n";
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
    memset(game_room, 0, sizeof(GameRoom));
    game_room->player_sockets[0] = clients[client1].sock;
    game_room->player_sockets[1] = clients[client2].sock;
    game_room->player_index[0] = client1;
    game_room->player_index[1] = client2;

    game_room->current_turn = 0; // Start with player 0

    init_plateau(&game_room->board); // Initialize the Awalé board

    // Notify both clients about the game start
    char start_msg[BUF_SIZE];
    snprintf(start_msg, BUF_SIZE, "Awalé game started between %s and %s. %s goes first.\n You can use /1 to /6 to make a move or /-1 to exit.\n You can also chat with other player.\n",
             clients[client1].name, clients[client2].name, clients[client1].name);
    char *board_state = afficher_plateau(&game_room->board);  // Get board state
    send_to_room(room_id, board_state);
    free(board_state);  // Free dynamically allocated memory
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
    send_player_list(clients, actual);
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
}


static void handle_disconnection(int client_index, int *actual) {
    close(clients[client_index].sock);
    remove_client(clients, client_index, actual);
    send_player_list(clients, *actual);
}

static void handle_outside_room(int client_index, char *buffer, int actual) {
    if (strcmp(buffer, "1") == 0) {
        send_player_list(clients, actual);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "2") == 0) {
        write_client(clients[client_index].sock, "Disconnecting...\n");
        handle_disconnection(client_index, &actual);
    } else if (strcmp(buffer, "3") == 0) {
        handle_join_game(client_index, actual);
    }else if (strcmp(buffer, "4") == 0) {
        handle_set_bio(client_index);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "5") == 0) {
        handle_view_bio(client_index, actual);
        send_welcome_message(&clients[client_index]);
    }else if (strcmp(buffer, "6") == 0) {
        list_ongoing_games(client_index);
    } else if (strncmp(buffer, "observe ", 8) == 0) {
        int room_id = atoi(buffer + 8);
        observe_game(client_index, room_id);
    }else if (clients[client_index].waiting_for_response) {
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
    } else{
        write_client(clients[client_index].sock, "Invalid option. Choose again.\n");
        send_welcome_message(&clients[client_index]);
    }
}

static void handle_in_room(int client_index, char *buffer) {
    int room_id = clients[client_index].room_id;

    if (room_id < 0 || room_id >= MAX_CLIENTS) {
        write_client(clients[client_index].sock, "Invalid room ID.\n");
        return;
    }

    GameRoom *game_room = &game_rooms[room_id];

    if (game_room == NULL) {
        write_client(clients[client_index].sock, "Game room does not exist.\n");
        return;
    }

    if (buffer[0] == '/') {  // Game command (starts with '/')
        int move = atoi(buffer + 1);  // Skip the '/' prefix
        if (move == -1) {
            // End game if a player inputs -1
            int opponent_index = game_room->player_index[1 - game_room->current_turn];
            char end_msg[BUF_SIZE];
            snprintf(end_msg, BUF_SIZE, "Player %s disconnected. You won!\n", clients[client_index].name);
            notify_observers(room_id, end_msg);

            // Notify the opponent
            write_client(game_room->player_sockets[1 - game_room->current_turn], end_msg);

            // Reset both players
            clients[client_index].in_room = 0;
            clients[client_index].room_id = -1;
            clients[opponent_index].in_room = 0;
            clients[opponent_index].room_id = -1;

            // Reset game room state
            memset(game_room, 0, sizeof(GameRoom));
            send_welcome_message(&clients[client_index]);
            send_welcome_message(&clients[opponent_index]);
            return;
        }

        if (move < 1 || move > 6) {
            write_client(clients[client_index].sock, "Invalid move. Use /1 to /6 or /-1 to exit.\n");
            return;
        }

        if (clients[client_index].sock == game_room->player_sockets[game_room->current_turn]) {
            int result = jouer_coup(&game_room->board, game_room->current_turn, move - 1);
            char *board_state = afficher_plateau(&game_room->board);
            send_to_room(room_id, board_state);
            notify_observers(room_id, board_state);
            free(board_state);

            if (result) {
                char end_msg[BUF_SIZE];
                snprintf(end_msg, BUF_SIZE, "Player %s wins!\n", clients[client_index].name);
                send_to_room(room_id, end_msg);
                notify_observers(room_id, end_msg);

                // Reset both players
                clients[game_room->player_index[0]].in_room = 0;
                clients[game_room->player_index[0]].room_id = -1;
                clients[game_room->player_index[1]].in_room = 0;
                clients[game_room->player_index[1]].room_id = -1;

                // Reset game room state
                memset(game_room, 0, sizeof(GameRoom));
            } else {
                game_room->current_turn = 1 - game_room->current_turn;
                snprintf(buffer, BUF_SIZE, "Player %s made a move. It's now Player %s's turn.\n",
                         clients[client_index].name,
                         clients[game_room->player_index[game_room->current_turn]].name);
                send_to_room(room_id, buffer);
                notify_observers(room_id, buffer);
                write_client(game_room->player_sockets[game_room->current_turn], "Your turn! Use /1 to /6 or /-1 to exit.\n");
            }
        } else {
            write_client(clients[client_index].sock, "Not your turn. Wait for the other player.\n");
        }
    } else {  // Chat message
        char chat_msg[BUF_SIZE];
        snprintf(chat_msg, BUF_SIZE, "%s: %s", clients[client_index].name, buffer);
        send_to_room(room_id, chat_msg);
        notify_observers(room_id, chat_msg);
    }
}

static void add_observer(int room_id, int observer_socket) {
    GameRoom *game_room = &game_rooms[room_id];

    if (game_room->observer_count >= MAX_CLIENTS) {
        write_client(observer_socket, "The game room is full. Cannot observe.\n");
        return;
    }

    game_room->observers[game_room->observer_count++] = observer_socket;

    char buffer[BUF_SIZE];
    snprintf(buffer, BUF_SIZE, "You are now observing Game Room %d.\n", room_id);
    write_client(observer_socket, buffer);

    // Show the current board state
    char *board_state = afficher_plateau(&game_room->board);
    write_client(observer_socket, board_state);
    free(board_state);
}

static void notify_observers(int room_id, const char *message) {
    GameRoom *game_room = &game_rooms[room_id];

    for (int i = 0; i < game_room->observer_count; i++) {
        write_client(game_room->observers[i], message);
    }
}

static void list_ongoing_games(int client_index) {
    char buffer[BUF_SIZE] = "Currently ongoing games:\n";

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game_rooms[i].player_sockets[0] > 0 && game_rooms[i].player_sockets[1] > 0) {
            char game_entry[128];
            snprintf(game_entry, sizeof(game_entry), "Room ID: %d | Players: %s vs %s | Observers: %d\n",
                     i,
                     clients[game_rooms[i].player_index[0]].name,
                     clients[game_rooms[i].player_index[1]].name,
                     game_rooms[i].observer_count);
            strncat(buffer, game_entry, sizeof(buffer) - strlen(buffer) - 1);
        }
    }

    if (strlen(buffer) == strlen("Currently ongoing games:\n")) {
        strncat(buffer, "No ongoing games.\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    write_client(clients[client_index].sock, buffer);
}

static void observe_game(int client_index, int room_id) {
    if (room_id < 0 || room_id >= MAX_CLIENTS) {
        write_client(clients[client_index].sock, "Invalid room ID.\n");
        return;
    }

    GameRoom *game_room = &game_rooms[room_id];

    if (game_room->player_sockets[0] == 0 || game_room->player_sockets[1] == 0) {
        write_client(clients[client_index].sock, "No active game in this room.\n");
        return;
    }

    add_observer(room_id, clients[client_index].sock);
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
