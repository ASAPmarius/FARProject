#include "chatroom.h"
#include <stdlib.h>
#include <string.h>

ChatRoom *chatroom_create(const char *name, int max_members) {
    ChatRoom *room = malloc(sizeof(ChatRoom));
    if (!room) return NULL;
    
    strncpy(room->name, name, sizeof(room->name) - 1);
    room->name[sizeof(room->name) - 1] = '\0'; // Ensure null-termination
    
    room->max_members = max_members;
    room->member_count = 0;
    room->active = 1;
    
    // Allocate memory for member indices
    room->member_indices = malloc(max_members * sizeof(int));
    if (!room->member_indices) {
        free(room);
        return NULL;
    }
    
    return room;
}

void chatroom_free(ChatRoom *room) {
    if (!room) return;
    
    free(room->member_indices);
    free(room);
}

int chatroom_add_member(ChatRoom *room, int client_index) {
    if (!room || room->member_count >= room->max_members) {
        return 0; // Room is full or invalid
    }
    
    // Check if client is already a member
    for (int i = 0; i < room->member_count; i++) {
        if (room->member_indices[i] == client_index) {
            return 0; // Already a member
        }
    }
    
    // Add client index to member array
    room->member_indices[room->member_count++] = client_index;
    return 1;
}

int chatroom_remove_member(ChatRoom *room, int client_index) {
    if (!room) return 0;
    
    // Find the client in the member array
    for (int i = 0; i < room->member_count; i++) {
        if (room->member_indices[i] == client_index) {
            // Found the client, remove by shifting array elements
            for (int j = i; j < room->member_count - 1; j++) {
                room->member_indices[j] = room->member_indices[j + 1];
            }
            room->member_count--;
            return 1;
        }
    }
    
    return 0; // Client not found
}

int chatroom_is_member(ChatRoom *room, int client_index) {
    if (!room) return 0;
    
    for (int i = 0; i < room->member_count; i++) {
        if (room->member_indices[i] == client_index) {
            return 1; // Client is a member
        }
    }
    
    return 0; // Client is not a member
}

int chatroom_get_member_count(ChatRoom *room) {
    return room ? room->member_count : 0;
}

int chatroom_get_max_members(ChatRoom *room) {
    return room ? room->max_members : 0;
}

int chatroom_is_full(ChatRoom *room) {
    return room ? (room->member_count >= room->max_members) : 1;
}

const char *chatroom_get_name(ChatRoom *room) {
    return room ? room->name : NULL;
}