#ifndef CLIENT_H
#define CLIENT_H

#include "server2.h"

typedef struct {
    int sock;
    char name[32];
    int in_room;   // 0 if waiting, 1 if in a private room
    int room_id;   // Room ID if in a private room
    int waiting_for_response;
} Client;

#endif /* guard */
