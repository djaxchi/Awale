#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h> 

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
    char game_file[256];     // File path for saving the game
    int friends_only;        // 1 if only friends can spectate, 0 otherwise
} GameRoom;

GameRoom game_rooms[MAX_CLIENTS];

static Client clients[MAX_CLIENTS];
static int room_counter = 0;


void ensure_file_exists(const char *filename) {
    FILE *file = fopen(filename, "a"); // Open in append mode to create the file if it doesn’t exist
    if (file) {
        fclose(file);
    } else {
        perror("Failed to create file");
        exit(EXIT_FAILURE); // Exit if the file cannot be created
    }
}

static void init(void) {
#ifdef WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err < 0) {
        puts("WSAStartup failed!");
        exit(EXIT_FAILURE);
    }
#endif
    ensure_file_exists("Database/friends.txt");
    ensure_file_exists("Database/friend_requests.txt");
    ensure_file_exists("Database/players.txt");
    ensure_file_exists("Database/bios.txt");
}

static void end(void) {
#ifdef WIN32
    WSACleanup();
#endif
}

static void send_player_list(Client *clients, int actual, int client_index) {
    char buffer[BUF_SIZE] = "Connected clients:\n";
    for (int i = 0; i < actual; i++) {
        if (clients[i].in_room == 0 && i != client_index) {
            strncat(buffer, clients[i].name, sizeof(buffer) - strlen(buffer) - 1);
            strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
        }
    }
    write_client(clients[client_index].sock, buffer);
}

static void send_welcome_message(Client *client) {
    char welcome_msg[BUF_SIZE];

    // Build the welcome message with username and current ELO ranking
    snprintf(welcome_msg, sizeof(welcome_msg), 
        "Hey %s! Your current ELO ranking is %d.\n"
        "Options:\n"
        "1. Show list of connected clients\n"
        "2. Disconnect\n"
        "3. Join Game\n"
        "4. Set/Update Bio\n"
        "5. View Player Bio\n"
        "6. List ongoing games\n"
        "To observe a game, type 'observe <room_id>'\n"
        "7. Send a friend request\n"
        "8. Accept/Decline Friend Request\n"
        "9. View Friends List\n"
        "10. See top players\n"
        "Game Review Options:\n"
        " - Type 'list games' to view completed games.\n"
        " - Type 'replay <filename>' to review a completed game.\n"
        " - Type 'next' or 'prev' to navigate through a replay.\n",
        client->name, get_elo_rating(client->name));

    // Send the welcome message
    write_client(client->sock, welcome_msg);
}


int fetch_bio(const char *name, char *bio, size_t bio_size) {
    FILE *file = fopen("Database/bios.txt", "r");
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
    FILE *file = fopen("Database/bios.txt", "r+");
    if (!file) {
        file = fopen("Database/bios.txt", "w");
        if (!file) {
            perror("Failed to open bios file for writing");
            return;
        }
    }

    char line[512];
    char temp_filename[] = "Database/bios_tmp.txt";
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
    remove("Database/bios.txt");
    rename(temp_filename, "Database/bios.txt");
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
    send_player_list(clients, actual, client_index);
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

void add_player_to_registry(const char *player_name) {
    FILE *file = fopen("Database/players.txt", "r+");
    if (!file) {
        perror("Failed to open players.txt");
        return;
    }

    char name[32];
    int elo;
    while (fscanf(file, "%31s %d", name, &elo) == 2) { // Read both name and ELO
        if (strcmp(name, player_name) == 0) {
            fclose(file);
            return; // Player already exists
        }
    }

    // Move to the end of the file to add the new player
    fseek(file, 0, SEEK_END);
    fprintf(file, "%s %d\n", player_name, 1000); // Default ELO is 1000
    fclose(file);
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
    initialize_game_file(game_room,clients[client1].name,clients[client2].name);

    // Notify both clients about the game start
    char start_msg[BUF_SIZE];
    snprintf(start_msg, BUF_SIZE, "Awalé game started between %s and %s. %s goes first.\n You can use /1 to /6 to make a move or /-1 to exit.\n You can also chat with other player.\n Use /friends-only to make your room private.\n",
             clients[client1].name, clients[client2].name, clients[client1].name);
    char *board_state = afficher_plateau(&game_room->board);  // Get board state
    send_to_room(room_id, board_state);
    save_game_state(game_room);
    free(board_state);  // Free dynamically allocated memory
    write_client(clients[client1].sock, start_msg);
    write_client(clients[client2].sock, start_msg);

    // Inform the first player to make a move
    write_client(game_room->player_sockets[0], "Your turn! Choose a pit (1-6):\n");
}


int player_exists(const char *player_name) {
    FILE *file = fopen("Database/players.txt", "r");
    if (!file) {
        perror("Failed to open players.txt");
        return 0;
    }

    char name[32];
    int elo;
    while (fscanf(file, "%31s %d", name, &elo) == 2) { // Read both name and ELO
        if (strcmp(name, player_name) == 0) {
            fclose(file);
            return 1; // Player exists
        }
    }

    fclose(file);
    return 0; // Player does not exist
}

//save game system

void initialize_game_file(GameRoom *game_room, const char *player1, const char *player2) {
    // Get the current time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // Generate a timestamp in the format YYYYMMDD_HHMMSS
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", t);

    // Generate a unique file name with the timestamp
    snprintf(game_room->game_file, sizeof(game_room->game_file),"Database/Games/%s_vs_%s_%s.txt", player1, player2, timestamp);

    FILE *file = fopen(game_room->game_file, "w");
    if (file == NULL) {
        perror("Failed to create game file");
        return;
    }

    // Write player names
    fprintf(file, "Players:\n%s\n%s\n", player1, player2);

    fprintf(file, "Game States:\n");
    fclose(file);
}

void save_game_state(GameRoom *game_room) {
    FILE *file = fopen(game_room->game_file, "a");
    if (file == NULL) {
        perror("Failed to open game file for appending");
        return;
    }

    enregistrer_plateau(file, &game_room->board); // Use enregistrer_plateau to format the board state
    fprintf(file, "\n");

    fclose(file);
}

void finalize_game_file(GameRoom *game_room, const char *result) {
    FILE *file = fopen(game_room->game_file, "a");
    if (file == NULL) {
        perror("Failed to open game file for finalizing");
        return;
    }

    fprintf(file, "Game Result: %s\n", result);
    fclose(file);
}

//friend system

int are_friends(const char *player1, const char *player2) {
    FILE *file = fopen("Database/friends.txt", "r");
    if (!file) {
        perror("Failed to open friends.txt");
        return 0; // Assume not friends if file doesn't exist
    }

    char line[64];
    while (fgets(line, sizeof(line), file)) {
        char *saved_player = strtok(line, "|");
        char *friend_list = strtok(NULL, "\n");

        if (saved_player && friend_list &&
            (strcmp(saved_player, player1) == 0 || strcmp(saved_player, player2) == 0)) {
            char *friend_name = strtok(friend_list, ",");
            while (friend_name) {
                if ((strcmp(saved_player, player1) == 0 && strcmp(friend_name, player2) == 0) ||
                    (strcmp(saved_player, player2) == 0 && strcmp(friend_name, player1) == 0)) {
                    fclose(file);
                    return 1; // They are friends
                }
                friend_name = strtok(NULL, ",");
            }
        }
    }

    fclose(file);
    return 0; // Not friends
}

void send_friend_request(const char *sender, const char *receiver) {
    FILE *file = fopen("Database/friend_requests.txt", "a+");
    if (!file) {
        perror("Failed to open friend_requests.txt");
        return;
    }

    char line[64];
    while (fgets(line, sizeof(line), file)) {
        char *saved_sender = strtok(line, "|");
        char *saved_receiver = strtok(NULL, "\n");

        if (saved_sender && saved_receiver &&
            strcmp(saved_sender, sender) == 0 &&
            strcmp(saved_receiver, receiver) == 0) {
            fclose(file);
            return; // Request already exists
        }
    }

    // Append the new request
    fprintf(file, "%s|%s\n", sender, receiver);
    fclose(file);
}

int friend_request_exists(const char *sender, const char *receiver) {
    FILE *file = fopen("Database/friend_requests.txt", "r");
    if (!file) {
        return 0; // Assume no request exists if file doesn't exist
    }

    char line[64];
    while (fgets(line, sizeof(line), file)) {
        char *saved_sender = strtok(line, "|");
        char *saved_receiver = strtok(NULL, "\n");

        if (saved_sender && saved_receiver &&
            strcmp(saved_sender, sender) == 0 &&
            strcmp(saved_receiver, receiver) == 0) {
            fclose(file);
            return 1; // Request exists
        }
    }

    fclose(file);
    return 0; // Request does not exist
}

int reciprocal_request_exists(const char *sender, const char *receiver) {
    FILE *file = fopen("Database/friend_requests.txt", "r");
    if (!file) {
        return 0; // No reciprocal request if file doesn't exist
    }

    char line[64];
    while (fgets(line, sizeof(line), file)) {
        char *saved_sender = strtok(line, "|");
        char *saved_receiver = strtok(NULL, "\n");

        if (saved_sender && saved_receiver &&
            strcmp(saved_sender, receiver) == 0 &&
            strcmp(saved_receiver, sender) == 0) {
            fclose(file);
            return 1; // Reciprocal request exists
        }
    }

    fclose(file);
    return 0; // No reciprocal request
}

int count_pending_requests(const char *player) {
    FILE *file = fopen("Database/friend_requests.txt", "r");
    if (!file) {
        return 0; // No requests if file doesn't exist
    }

    char line[64];
    int count = 0;

    while (fgets(line, sizeof(line), file)) {
        char *receiver = strtok(NULL, "\n");
        if (receiver && strcmp(receiver, player) == 0) {
            count++;
        }
    }

    fclose(file);
    return count;
}

static void handle_send_friend_request(int client_index) {
    const char *prompt = "Enter the name of the player you want to send a friend request to:\n";
    write_client(clients[client_index].sock, prompt);

    char buffer[32];
    int n = read_client(clients[client_index].sock, buffer);
    if (n > 0) {
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

        // Prevent sending request to oneself
        if (strcmp(clients[client_index].name, buffer) == 0) {
            write_client(clients[client_index].sock, "You cannot send a friend request to yourself.\n");
            return;
        }

        // Check if player exists
        if (!player_exists(buffer)) {
            write_client(clients[client_index].sock, "Player does not exist.\n");
            return;
        }

        // Check if already friends
        if (are_friends(clients[client_index].name, buffer)) {
            write_client(clients[client_index].sock, "You are already friends with this player.\n");
            return;
        }

        // Prevent duplicate requests
        if (friend_request_exists(clients[client_index].name, buffer)) {
            write_client(clients[client_index].sock, "You have already sent a friend request to this player.\n");
            return;
        }

        // Prevent reciprocal requests
        if (reciprocal_request_exists(clients[client_index].name, buffer)) {
            write_client(clients[client_index].sock, "This player has already sent you a friend request. Check your pending requests.\n");
            return;
        }

        // Enforce request cap
        if (count_pending_requests(buffer) >= 15) {
            write_client(clients[client_index].sock, "This player has reached the maximum number of pending friend requests.\n");
            return;
        }

        // Send friend request
        send_friend_request(clients[client_index].name, buffer);
        write_client(clients[client_index].sock, "Friend request sent.\n");
    } else {
        write_client(clients[client_index].sock, "Failed to send friend request. Try again.\n");
    }
}

int fetch_pending_requests(const char *receiver, char requests[][32], int *num_requests) {
    FILE *file = fopen("Database/friend_requests.txt", "r");
    if (!file) {
        perror("Failed to open friend_requests.txt");
        *num_requests = 0;
        return 0;
    }

    char line[64];
    *num_requests = 0;

    while (fgets(line, sizeof(line), file)) {
        char *sender = strtok(line, "|");
        char *saved_receiver = strtok(NULL, "\n");

        if (sender && saved_receiver && strcmp(saved_receiver, receiver) == 0) {
            strncpy(requests[*num_requests], sender, 32);
            (*num_requests)++;
        }
    }

    fclose(file);
    return 1; // Success
}

static void handle_view_pending_requests(int client_index) {
    char requests[15][32]; // Max 15 pending requests
    int num_requests = 0;

    if (!fetch_pending_requests(clients[client_index].name, requests, &num_requests)) {
        write_client(clients[client_index].sock, "No pending friend requests.\n");
        return;
    }

    char buffer[512] = "Pending friend requests:\n";
    for (int i = 0; i < num_requests; i++) {
        strncat(buffer, requests[i], sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    if (num_requests == 0) {
        strncat(buffer, "No pending friend requests.\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    write_client(clients[client_index].sock, buffer);
}

void accept_friend_request(const char *player, const char *friend_name) {
    FILE *file = fopen("Database/friends.txt", "r");
    FILE *temp = fopen("Database/friends_tmp.txt", "w");
    if (!file || !temp) {
        perror("Failed to open friends.txt or temporary file");
        if (file) fclose(file);
        if (temp) fclose(temp);
        return;
    }

    char line[512];
    int player_found = 0;
    int friend_found = 0;

    // Process the file and add the new friendship
    while (fgets(line, sizeof(line), file)) {
        char *saved_player = strtok(line, "|");
        char *friends = strtok(NULL, "\n");

        if (saved_player) {
            if (strcmp(saved_player, player) == 0) {
                player_found = 1;
                fprintf(temp, "%s|%s%s%s\n", saved_player, friends ? friends : "",
                        friends ? "," : "", friend_name);
            } else if (strcmp(saved_player, friend_name) == 0) {
                friend_found = 1;
                fprintf(temp, "%s|%s%s%s\n", saved_player, friends ? friends : "",
                        friends ? "," : "", player);
            } else {
                fprintf(temp, "%s|%s\n", saved_player, friends ? friends : "");
            }
        }
    }

    // If player or friend doesn't exist in the file, add them
    if (!player_found) {
        fprintf(temp, "%s|%s\n", player, friend_name);
    }
    if (!friend_found) {
        fprintf(temp, "%s|%s\n", friend_name, player);
    }

    fclose(file);
    fclose(temp);
    remove("Database/friends.txt");
    rename("Database/friends_tmp.txt", "Database/friends.txt");
}

void remove_friend_request(const char *sender, const char *receiver) {
    FILE *file = fopen("Database/friend_requests.txt", "r");
    if (!file) {
        perror("Failed to open friend_requests.txt");
        return;
    }

    FILE *temp = fopen("Database/friend_requests_tmp.txt", "w");
    if (!temp) {
        perror("Failed to open temporary file for friend_requests.txt");
        fclose(file);
        return;
    }

    char line[64];
    while (fgets(line, sizeof(line), file)) {
        char *saved_sender = strtok(line, "|");
        char *saved_receiver = strtok(NULL, "\n");

        if (saved_sender && saved_receiver &&
            !(strcmp(saved_sender, sender) == 0 && strcmp(saved_receiver, receiver) == 0)) {
            fprintf(temp, "%s|%s\n", saved_sender, saved_receiver);
        }
    }

    fclose(file);
    fclose(temp);
    remove("Database/friend_requests.txt");
    rename("Database/friend_requests_tmp.txt", "Database/friend_requests.txt");
}

static void handle_accept_friend_request(int client_index) {
    char requests[15][32]; // Max 15 pending requests
    int num_requests = 0;

    // Fetch pending requests
    if (!fetch_pending_requests(clients[client_index].name, requests, &num_requests)) {
        write_client(clients[client_index].sock, "No pending friend requests.\n");
        return;
    }

    if (num_requests == 0) {
        write_client(clients[client_index].sock, "No pending friend requests.\n");
        return;
    }
    char buffer[512] = "Pending friend requests:\n";
    for (int i = 0; i < num_requests; i++) {
        char temp[64];
        snprintf(temp, sizeof(temp), "%d. %s\n", i + 1, requests[i]);
        strncat(buffer, temp, sizeof(buffer) - strlen(buffer) - 1);
    }

    strncat(buffer, "Enter the number of the request to accept or decline (e.g., '1 accept' or '2 decline'):\n", sizeof(buffer) - strlen(buffer) - 1);
    write_client(clients[client_index].sock, buffer);

    char response[64];
    int n = read_client(clients[client_index].sock, response);
    if (n > 0) {
        response[n] = '\0';
        response[strcspn(response, "\n")] = '\0'; // Remove newline

        int choice;
        char action[10];
        if (sscanf(response, "%d %s", &choice, action) == 2 && choice > 0 && choice <= num_requests) {
            const char *selected_request = requests[choice - 1];

            if (strcmp(action, "accept") == 0) {
                accept_friend_request(clients[client_index].name, selected_request);
                remove_friend_request(selected_request, clients[client_index].name);
                write_client(clients[client_index].sock, "Friend request accepted.\n");
            } else if (strcmp(action, "decline") == 0) {
                remove_friend_request(selected_request, clients[client_index].name);
                write_client(clients[client_index].sock, "Friend request declined.\n");
            } else {
                write_client(clients[client_index].sock, "Invalid action. Use 'accept' or 'decline'.\n");
            }
        } else {
            write_client(clients[client_index].sock, "Invalid input. Try again.\n");
        }
    } else {
        write_client(clients[client_index].sock, "Failed to read input. Try again.\n");
    }
}

int fetch_friends(const char *player, char friends[][32], int *num_friends) {
    FILE *file = fopen("Database/friends.txt", "r");
    if (!file) {
        perror("Failed to open friends.txt");
        *num_friends = 0;
        return 0; // No friends if file doesn't exist
    }

    char line[512];
    *num_friends = 0;

    while (fgets(line, sizeof(line), file)) {
        char *saved_player = strtok(line, "|");
        char *friend_list = strtok(NULL, "\n");

        if (saved_player && strcmp(saved_player, player) == 0 && friend_list) {
            char *friend_name = strtok(friend_list, ",");
            while (friend_name) {
                strncpy(friends[*num_friends], friend_name, 32);
                (*num_friends)++;
                friend_name = strtok(NULL, ",");
            }
            break; // We found the player; no need to continue
        }
    }

    fclose(file);
    return 1; // Success
}

static void handle_view_friends_list(int client_index) {
    char friends[50][32]; // Max 50 friends
    int num_friends = 0;

    // Fetch friends
    if (!fetch_friends(clients[client_index].name, friends, &num_friends)) {
        write_client(clients[client_index].sock, "You have no friends.\n");
        return;
    }

    // If no friends, inform the player
    if (num_friends == 0) {
        write_client(clients[client_index].sock, "You have no friends.\n");
        return;
    }

    // Display friends list
    char buffer[512] = "Your friends:\n";
    for (int i = 0; i < num_friends; i++) {
        strncat(buffer, friends[i], sizeof(buffer) - strlen(buffer) - 1);
        strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    write_client(clients[client_index].sock, buffer);
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
    add_player_to_registry(c.name);
    send_welcome_message(&c);
}

static void handle_disconnection(int client_index, int *actual) {
    close(clients[client_index].sock);
    remove_client(clients, client_index, actual);
    send_player_list(clients, *actual, client_index);
}

static void add_observer(int room_id, int observer_socket,int client_index) {
    GameRoom *game_room = &game_rooms[room_id];

    if (game_room->observer_count >= MAX_CLIENTS) {
        write_client(observer_socket, "The game room is full. Cannot observe.\n");
        return;
    }

    game_room->observers[game_room->observer_count++] = observer_socket;
    clients[client_index].observing = 1;
    clients[client_index].room_id = room_id;
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
      // Check if the room is "friends-only"
    if (game_room->friends_only) {
        int is_friend = are_friends(clients[client_index].name, 
                                    clients[game_room->player_index[0]].name) ||
                        are_friends(clients[client_index].name, 
                                    clients[game_room->player_index[1]].name);

        if (!is_friend) {
            write_client(clients[client_index].sock, "You can only observe games where you're friends with a player.\n");
            return;
        }
    }

    add_observer(room_id, clients[client_index].sock, client_index);
}

static void toggle_friends_only(int client_index) {
    int room_id = clients[client_index].room_id;

    if (room_id < 0 || room_id >= MAX_CLIENTS) {
        write_client(clients[client_index].sock, "You are not in a game room.\n");
        return;
    }

    GameRoom *game_room = &game_rooms[room_id];

    // Check if the client is one of the players
    if (game_room->player_sockets[0] != clients[client_index].sock &&
        game_room->player_sockets[1] != clients[client_index].sock) {
        write_client(clients[client_index].sock, "Only players can toggle spectator privacy.\n");
        return;
    }

    // Toggle the "friends-only" setting
    game_room->friends_only = !game_room->friends_only;

    char buffer[BUF_SIZE];
    snprintf(buffer, BUF_SIZE, "Spectator mode updated: %s.\n",
             game_room->friends_only ? "Friends-only" : "Public");
    write_client(clients[client_index].sock, buffer);
}

static void list_saved_games(int client_index) {
    DIR *dir = opendir("Database/Games");
    if (!dir) {
        write_client(clients[client_index].sock, "Failed to open the games directory.\n");
        return;
    }

    char buffer[BUF_SIZE] = "Completed games:\n";
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Only regular files
            strncat(buffer, entry->d_name, sizeof(buffer) - strlen(buffer) - 1);
            strncat(buffer, "\n", sizeof(buffer) - strlen(buffer) - 1);
        }
    }

    closedir(dir);

    if (strlen(buffer) == strlen("Completed games:\n")) {
        strncat(buffer, "No completed games found.\n", sizeof(buffer) - strlen(buffer) - 1);
    }

    write_client(clients[client_index].sock, buffer);
}

static void replay_game(int client_index, const char *game_filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "Database/Games/%s", game_filename);

    FILE *file = fopen(filepath, "r");
    if (!file) {
        write_client(clients[client_index].sock, "Failed to open the game file. Ensure the filename is correct.\n");
        return;
    }

    char buffer[BUF_SIZE];
    write_client(clients[client_index].sock, "Replaying game:\n");

    while (fgets(buffer, sizeof(buffer), file)) {
        write_client(clients[client_index].sock, buffer);
        usleep(500000);  // Pause for half a second between lines for readability
    }

    fclose(file);
    write_client(clients[client_index].sock, "End of game replay.\n");
}

typedef struct {
    char **states;  // Array of game states
    int count;      // Total number of states
    int current;    // Current state index
} ReplaySession;

static ReplaySession replay_sessions[MAX_CLIENTS];

void start_replay_session(int client_index, const char *game_filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "Database/Games/%s", game_filename);

    FILE *file = fopen(filepath, "r");
    if (!file) {
        write_client(clients[client_index].sock, "Failed to open the game file.\n");
        return;
    }

    char line[BUF_SIZE];
    ReplaySession *session = &replay_sessions[client_index];
    session->count = 0;
    session->current = 0;
    session->states = malloc(sizeof(char *) * 100);  // Max 100 states

    while (fgets(line, sizeof(line), file)) {
        session->states[session->count] = strdup(line);
        session->count++;
    }

    fclose(file);
    write_client(clients[client_index].sock, "Replay session started. Use 'next' or 'prev' to navigate.\n");
    write_client(clients[client_index].sock, session->states[0]);  // Display first state
}

void navigate_replay_session(int client_index, const char *command) {
    ReplaySession *session = &replay_sessions[client_index];

    if (strcmp(command, "next") == 0) {
        if (session->current + 1 < session->count) {
            session->current++;
            write_client(clients[client_index].sock, session->states[session->current]);
        } else {
            write_client(clients[client_index].sock, "You are at the last state.\n");
        }
    } else if (strcmp(command, "prev") == 0) {
        if (session->current > 0) {
            session->current--;
            write_client(clients[client_index].sock, session->states[session->current]);
        } else {
            write_client(clients[client_index].sock, "You are at the first state.\n");
        }
    } else {
        write_client(clients[client_index].sock, "Invalid command. Use 'next' or 'prev'.\n");
    }
}

//elo ranking system

static void get_top_elo(char *output, size_t output_size) {
    FILE *file = fopen("Database/players.txt", "r");
    if (!file) {
        perror("Failed to open players database");
        snprintf(output, output_size, "Unable to fetch top players at the moment.\n");
        return;
    }

    char names[100][32];  // Maximum of 100 players, with max 32 chars for names
    int elos[100];
    int count = 0;

    // Read players from the file
    while (fscanf(file, "%31s %d", names[count], &elos[count]) == 2) {
        count++;
    }
    fclose(file);

    // Sort players by ELO in descending order
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (elos[i] < elos[j]) {
                // Swap ELOs
                int temp_elo = elos[i];
                elos[i] = elos[j];
                elos[j] = temp_elo;

                // Swap names
                char temp_name[32];
                strncpy(temp_name, names[i], sizeof(temp_name));
                strncpy(names[i], names[j], sizeof(names[i]));
                strncpy(names[j], temp_name, sizeof(names[j]));
            }
        }
    }

    // Format the top 5 players
    snprintf(output, output_size, "Top 5 Players:\n");
    for (int i = 0; i < count && i < 5; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%d. %s: %d ELO\n", i + 1, names[i], elos[i]);
        strncat(output, line, output_size - strlen(output) - 1);
    }
}

void update_player_elo(const char *player_name, int result) {
    // result: 1 for win, -1 for loss
    const int elo_change = 30; // Points added/subtracted per game
    const char *file_path = "Database/players.txt";

    FILE *file = fopen(file_path, "r");
    if (!file) {
        perror("Failed to open players database for reading");
        return;
    }

    char temp_file_path[] = "Database/players_temp.txt";
    FILE *temp_file = fopen(temp_file_path, "w");
    if (!temp_file) {
        perror("Failed to open temporary file for writing");
        fclose(file);
        return;
    }

    char name[32];
    int elo;
    int found = 0;

    // Read each line, update the player's ELO if found, and write to the temp file
    while (fscanf(file, "%31s %d", name, &elo) == 2) {
        if (strcmp(name, player_name) == 0) {
            elo += result * elo_change; // Update the ELO based on the result
            found = 1;
        }
        fprintf(temp_file, "%s %d\n", name, elo);
    }

    // If the player was not found, add them to the file with a starting ELO
    if (!found) {
        int initial_elo = 1000 + result * elo_change;
        fprintf(temp_file, "%s %d\n", player_name, initial_elo);
    }

    fclose(file);
    fclose(temp_file);

    // Replace the original file with the updated one
    if (remove(file_path) != 0) {
        perror("Failed to remove original players database");
        return;
    }
    if (rename(temp_file_path, file_path) != 0) {
        perror("Failed to rename temporary file to players database");
    }
}

int get_elo_rating(const char *player_name){
    FILE *file = fopen("Database/players.txt", "r");
    if (!file) {
        perror("Failed to open players database");
        return 0;
    }

    char name[32];
    int elo;
    while (fscanf(file, "%31s %d", name, &elo) == 2) {
        if (strcmp(name, player_name) == 0) {
            fclose(file);
            return elo;
        }
    }

    fclose(file);
    return 0;
}


static void handle_outside_room(int client_index, char *buffer, int actual) {
    if (clients[client_index].observing) {
        if (strcmp(buffer, "exit") == 0) {
            // Remove the client from the observers list
            GameRoom *game_room = &game_rooms[clients[client_index].room_id];
            for (int j = 0; j < MAX_CLIENTS; j++) {
                if (game_room->observers[j] == clients[client_index].sock) {
                    game_room->observers[j] = 0;
                    break;
                }
            }

            clients[client_index].observing = 0;
            clients[client_index].room_id = -1;

            write_client(clients[client_index].sock, "You have left observation mode.\n");
            send_welcome_message(&clients[client_index]);
        } else {
            write_client(clients[client_index].sock, "Invalid command. Type 'exit' to leave observation mode.\n");
        }
    }else if (clients[client_index].waiting_for_response) {
            char *target_name = buffer;
            target_name[strcspn(target_name, "\n")] = '\0';
            send_duel_request(client_index, target_name, actual);
    } else if(strcmp(buffer, "1") == 0) {
        send_player_list(clients, actual, client_index);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "2") == 0) {
        write_client(clients[client_index].sock, "Disconnecting...\n");
        handle_disconnection(client_index, &actual);
    } else if (strcmp(buffer, "3") == 0) {
        handle_join_game(client_index, actual);
    } else if (strcmp(buffer, "4") == 0) {
        handle_set_bio(client_index);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "5") == 0) {
        handle_view_bio(client_index, actual);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "6") == 0) {
        list_ongoing_games(client_index);        
    } else if (strcmp(buffer, "7") ==0){
        handle_send_friend_request(client_index);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "8") == 0) {
        handle_accept_friend_request(client_index);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "9") == 0) {
        handle_view_friends_list(client_index);
        send_welcome_message(&clients[client_index]);
    } else if (strcmp(buffer, "10") == 0) {
        char top_players[BUF_SIZE];
        get_top_elo(top_players, sizeof(top_players));
        write_client(clients[client_index].sock, top_players);
        send_welcome_message(&clients[client_index]);
    }else if (strncmp(buffer, "observe ", 8) == 0) {
        int room_id = atoi(buffer + 8);
        observe_game(client_index, room_id);
    } else if (strcmp(buffer, "list games") == 0) {
        list_saved_games(client_index);
    } else if (strncmp(buffer, "replay ", 7) == 0) {
        char *game_filename = buffer + 7;
        start_replay_session(client_index, game_filename);
    } else if (strcmp(buffer, "next") == 0 || strcmp(buffer, "prev") == 0) {
        navigate_replay_session(client_index, buffer);
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

    if (strcmp(buffer, "/friends-only") == 0) {
        toggle_friends_only(client_index);
        return;
    }

    if (buffer[0] == '/') {  // Game command (starts with '/')
        int move = atoi(buffer + 1);  // Skip the '/' prefix
        if (move == -1) {
            // End game if a player inputs -1
            int opponent_index = game_room->player_index[1 - game_room->current_turn];
            char end_msg[BUF_SIZE];
            snprintf(end_msg, BUF_SIZE, "Player %s disconnected. You won!\n", clients[client_index].name);
            update_player_elo(clients[client_index].name, -1);
            update_player_elo(clients[opponent_index].name, 1);
            notify_observers(room_id, end_msg);
            finalize_game_file(game_room, end_msg); // Replace with actual result

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
            save_game_state(game_room);
            free(board_state);

            if (result) {
                int opponent_index = game_room->player_index[1 - game_room->current_turn];
                char end_msg[BUF_SIZE];
                snprintf(end_msg, BUF_SIZE, "Player %s wins!\n", clients[client_index].name);
                update_player_elo(clients[client_index].name, 1);
                update_player_elo(clients[opponent_index].name, -1);

                send_to_room(room_id, end_msg);
                finalize_game_file(game_room, end_msg); // Replace with actual result
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
