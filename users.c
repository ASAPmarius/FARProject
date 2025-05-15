#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "dict.h"

#define MAX_USERNAME_LENGTH 50

/**
 * Save all users to a text file
 * Format: username:password
 * (In a real app, passwords should be hashed)
 */
int users_save_all(const char *filename, SimpleDict *users_dict) {
    if (!filename || !users_dict) return 0;
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Error opening file for saving users");
        return 0;
    }
    
    // Iterate through all entries in the dictionary
    for (size_t i = 0; i < users_dict->count; i++) {
        Entry *entry = &users_dict->entries[i];
        // Save in format: username:client_index
        fprintf(file, "%s:%s\n", entry->key, entry->value);
    }
    
    fclose(file);
    return 1;
}

/**
 * Load users from a text file into the dictionary
 */
int users_load_all(const char *filename, SimpleDict *users_dict) {
    if (!filename || !users_dict) return 0;
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        // File doesn't exist yet - not an error for first run
        if (errno == ENOENT) {
            return 1;
        }
        
        perror("Error opening file for loading users");
        return 0;
    }
    
    char line[MAX_USERNAME_LENGTH * 2];
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline if present
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Parse line (format: username:client_index)
        char username[MAX_USERNAME_LENGTH] = {0};
        char client_index[20] = {0};
        
        char *colon = strchr(line, ':');
        if (!colon) continue; // Invalid line format
        
        size_t username_len = colon - line;
        if (username_len >= MAX_USERNAME_LENGTH) username_len = MAX_USERNAME_LENGTH - 1;
        
        strncpy(username, line, username_len);
        username[username_len] = '\0';
        
        strncpy(client_index, colon + 1, sizeof(client_index) - 1);
        
        // Insert into dictionary
        dict_insert(users_dict, username, client_index);
    }
    
    fclose(file);
    return 1;
}