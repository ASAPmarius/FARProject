#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "dict.h"

#define MAX_USERNAME_LENGTH 50
#define MAX_PASSWORD_LENGTH 50

/**
 * Save all users to a text file
 * Format: username:password
 * (En vrai, les mots de passe devraient être hachés)
 */
int users_save_all(const char *filename, SimpleDict *users_dict) {
    if (!filename || !users_dict) return 0;

    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Error opening file for saving users");
        return 0;
    }

    // Parcours de toutes les entrées du dictionnaire
    for (size_t i = 0; i < users_dict->count; i++) {
        Entry *entry = &users_dict->entries[i];
        // Sauvegarde au format: username:password
        fprintf(file, "%s:%s\n", entry->key, entry->value);
    }

    fclose(file);
    return 1;
}

/**
 * Load users from a text file into the dictionary
 * On attend chaque ligne au format username:password
 */
int users_load_all(const char *filename, SimpleDict *users_dict) {
    if (!filename || !users_dict) return 0;

    FILE *file = fopen(filename, "r");
    if (!file) {
        // Le fichier n'existe pas encore – pas une erreur au premier lancement
        if (errno == ENOENT) {
            return 1;
        }
        perror("Error opening file for loading users");
        return 0;
    }

    char line[MAX_USERNAME_LENGTH + MAX_PASSWORD_LENGTH + 2]; // pour "username:password\n"
    while (fgets(line, sizeof(line), file)) {
        // Supprimer le '\n'
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        // Rechercher le séparateur ':'
        char *colon = strchr(line, ':');
        if (!colon) continue;  // ligne malformée

        // Extraire username et password
        *colon = '\0';
        char *username = line;
        char *password = colon + 1;

        // Limiter la longueur
        username[MAX_USERNAME_LENGTH - 1] = '\0';
        password[MAX_PASSWORD_LENGTH - 1] = '\0';

        // Insérer dans le dictionnaire
        dict_insert(users_dict, username, password);
    }

    fclose(file);
    return 1;
}
