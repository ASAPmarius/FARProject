#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "globalVariables.h"
#include "dict.h"
#include "chatroom.h"

#define BUFFER_SIZE 1000
#define MAX_USERS 100  // Maximum d'utilisateurs simultanés

// Structure pour stocker les informations complètes d'un client
typedef struct {
    char username[50];               // Nom d'utilisateur
    struct sockaddr_in addr;         // Structure d'adresse complète
    int active;                      // Flag pour indiquer si l'entrée est active
    int joined_rooms[MAX_ROOMS];     // Salles auxquelles le client a adhéré
    int room_count;                  // Nombre de salles auxquelles le client a adhéré
} ClientInfo;

// Variables globales
int dS;                              // Socket du serveur
SimpleDict *users_dict;              // Dictionnaire des utilisateurs
ClientInfo *clients;                 // Tableau des informations clients
int client_count = 0;                // Nombre de clients
ChatRoom *rooms[MAX_ROOMS];          // Tableau des salles de chat
int room_count = 0;                  // Nombre de salles de chat
SimpleDict *room_dict;               // Dictionnaire des salles de chat (nom -> index)

// Fonction pour diffuser un message à tous les membres d'une salle
void broadcast_to_room(int room_index, const char *message, const char *sender_username, struct sockaddr_in *sender_addr) {
    if (room_index < 0 || room_index >= room_count || !rooms[room_index]) {
        return;
    }
    
    ChatRoom *room = rooms[room_index];
    char forward_msg[BUFFER_SIZE];
    sprintf(forward_msg, "[%s] %s: %s", room->name, sender_username, message);
    
    // Parcourir tous les membres de la salle
    for (int i = 0; i < room->member_count; i++) {
        int member_index = room->member_indices[i];
        
        // Ne pas envoyer au client qui a envoyé le message
        if (clients[member_index].active && 
            (sender_addr == NULL ||
             clients[member_index].addr.sin_addr.s_addr != sender_addr->sin_addr.s_addr ||
             clients[member_index].addr.sin_port != sender_addr->sin_port)) {
            
            // Envoyer le message au membre
            if (sendto(dS, forward_msg, strlen(forward_msg), 0,
                     (struct sockaddr*) &clients[member_index].addr,
                     sizeof(struct sockaddr_in)) < 0) {
                perror("Erreur envoi message à un membre");
            }
        }
    }
}

// Fonction pour trouver l'index d'un client à partir de son adresse
int find_client_index(struct sockaddr_in *addr) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active &&
            clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i].addr.sin_port == addr->sin_port) {
            return i;
        }
    }
    return -1;
}

// Fonction pour trouver la salle par son nom
int find_room_by_name(const char *room_name) {
    const char *index_str = dict_get(room_dict, room_name);
    if (!index_str) return -1;
    return atoi(index_str);
}

void save_users_to_file(const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Error opening users file for writing");
        return;
    }
    
    // Iterate through the clients array and save active and inactive users
    for (int i = 0; i < client_count; i++) {
        fprintf(file, "%d:%s\n", i, clients[i].username);
    }
    
    fclose(file);
    printf("Users saved to %s\n", filename);
}

// Function to save rooms to file
void save_rooms_to_file(const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Error opening rooms file for writing");
        return;
    }
    
    // Save each room with its capacity and members
    for (int i = 0; i < room_count; i++) {
        if (rooms[i] && rooms[i]->active) {
            // Start with room name and capacity
            fprintf(file, "%s:%d:", rooms[i]->name, rooms[i]->max_members);
            
            // Add member IDs separated by commas
            for (int j = 0; j < rooms[i]->member_count; j++) {
                fprintf(file, "%d", rooms[i]->member_indices[j]);
                // Add comma if not the last member
                if (j < rooms[i]->member_count - 1) {
                    fprintf(file, ",");
                }
            }
            fprintf(file, "\n");
        }
    }
    
    fclose(file);
    printf("Rooms saved to %s\n", filename);
}

// Function to load users from file
void load_users_from_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        // It's okay if the file doesn't exist yet
        printf("No users file found. Starting with empty users list.\n");
        return;
    }
    
    char line[100];
    int max_id = -1;
    
    // Read each line from the file
    while (fgets(line, sizeof(line), file)) {
        int id;
        char username[50];
        
        // Parse the line in format "id:username"
        if (sscanf(line, "%d:%49s", &id, username) == 2) {
            // Update client_count if we find a larger ID
            if (id > max_id) {
                max_id = id;
            }
            
            // Add to users_dict
            char id_str[10];
            sprintf(id_str, "%d", id);
            dict_insert(users_dict, username, id_str);
            
            // Update clients array
            if (id < MAX_USERS) {
                strncpy(clients[id].username, username, sizeof(clients[id].username) - 1);
                clients[id].active = 0; // Start as inactive
                clients[id].room_count = 0;
            }
        }
    }
    
    // Update client_count to be one more than the max ID found
    client_count = max_id + 1;
    
    fclose(file);
    printf("Loaded %d users from %s\n", client_count, filename);
}

// Function to load rooms from file
void load_rooms_from_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        // It's okay if the file doesn't exist yet
        printf("No rooms file found. Starting with empty rooms list.\n");
        return;
    }
    
    char line[256];
    room_count = 0;
    
    // Read each line from the file
    while (fgets(line, sizeof(line), file) && room_count < MAX_ROOMS) {
        char room_name[MAX_ROOM_NAME_LENGTH];
        int max_members;
        char member_ids_str[200] = "";
        
        // Remove newline if present
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Find the first and second colon
        char* first_colon = strchr(line, ':');
        if (!first_colon) continue;
        
        char* second_colon = strchr(first_colon + 1, ':');
        if (!second_colon) continue;
        
        // Extract room name
        size_t name_len = first_colon - line;
        if (name_len >= MAX_ROOM_NAME_LENGTH) name_len = MAX_ROOM_NAME_LENGTH - 1;
        strncpy(room_name, line, name_len);
        room_name[name_len] = '\0';
        
        // Extract max members
        *second_colon = '\0'; // Temporarily null-terminate for atoi
        max_members = atoi(first_colon + 1);
        *second_colon = ':'; // Restore the colon
        
        // Create the room
        ChatRoom* new_room = chatroom_create(room_name, max_members);
        if (!new_room) continue;
        
        // Get the member IDs string
        strncpy(member_ids_str, second_colon + 1, sizeof(member_ids_str) - 1);
        
        // Parse the member IDs
        char* token = strtok(member_ids_str, ",");
        while (token) {
            int member_id = atoi(token);
            
            // Only add if the member ID is valid and the user exists
            if (member_id >= 0 && member_id < client_count) {
                chatroom_add_member(new_room, member_id);
            }
            
            token = strtok(NULL, ",");
        }
        
        // Add room to rooms array and dictionary
        rooms[room_count] = new_room;
        char index_str[10];
        sprintf(index_str, "%d", room_count);
        dict_insert(room_dict, room_name, index_str);
        
        room_count++;
    }
    
    fclose(file);
    printf("Loaded %d rooms from %s\n", room_count, filename);
}

// Gestionnaire de signal pour fermeture propre
void handle_signal(int sig) {
    printf("\nFermeture du serveur (signal %d)...\n", sig);
    
    // Save state to files
    save_users_to_file("users.txt");
    save_rooms_to_file("rooms.txt");
    
    // Libération des ressources
    for (int i = 0; i < room_count; i++) {
        chatroom_free(rooms[i]);
    }
    dict_free(users_dict);
    dict_free(room_dict);
    free(clients);
    close(dS);
    
    exit(EXIT_SUCCESS);
}

void handle_new_user_login(const char* username, struct sockaddr_in aE, socklen_t lgA) {
    // Nouvel utilisateur - ajouter au dictionnaire avec un index dans le tableau
    char index_str[10];
    sprintf(index_str, "%d", client_count);
    dict_insert(users_dict, username, index_str);
    
    // Stocker les informations complètes du client
    if (client_count < MAX_USERS) {
        strncpy(clients[client_count].username, username, sizeof(clients[client_count].username) - 1);
        clients[client_count].addr = aE;  // Copie complète de la structure d'adresse
        clients[client_count].active = 1;
        clients[client_count].room_count = 0;
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(aE.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Nouvel utilisateur enregistré: %s @ %s:%d (index: %d)\n", 
              username, client_ip, ntohs(aE.sin_port), client_count);
        
        // Envoyer confirmation
        char response[BUFFER_SIZE];
        sprintf(response, "Bienvenue %s! Vous êtes connecté.", username);
        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
        
        client_count++;
    } else {
        // Limite d'utilisateurs atteinte
        dict_remove(users_dict, username);  // Annuler l'insertion dans le dictionnaire
        
        char response[BUFFER_SIZE] = "Erreur: Limite d'utilisateurs atteinte.";
        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
    }
}

int main(int argc, char *argv[]) {
    printf("Début programme serveur UDP\n");

    // Configuration du gestionnaire de signaux
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Création de la socket UDP
    dS = socket(PF_INET, SOCK_DGRAM, 0);
    if (dS == -1) {
        perror("Erreur création socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket UDP Créée\n");

    // Configuration de l'adresse locale
    struct sockaddr_in aL;
    aL.sin_family = AF_INET;
    aL.sin_addr.s_addr = INADDR_ANY;
    aL.sin_port = htons(serverPort);

    // Nommage de la socket
    if (bind(dS, (struct sockaddr*) &aL, sizeof(aL)) == -1) {
        perror("Erreur bind");
        exit(EXIT_FAILURE);
    }
    printf("Socket Nommée sur port %d\n", serverPort);

    // Création du dictionnaire des utilisateurs (pour les recherches par nom)
    users_dict = dict_create();
    if (!users_dict) {
        perror("Erreur création dictionnaire utilisateurs");
        exit(EXIT_FAILURE);
    }
    
    // Création du dictionnaire des salles (pour les recherches par nom)
    room_dict = dict_create();
    if (!room_dict) {
        perror("Erreur création dictionnaire salles");
        dict_free(users_dict);
        exit(EXIT_FAILURE);
    }
    
    // Création du tableau des informations clients
    clients = malloc(MAX_USERS * sizeof(ClientInfo));
    if (!clients) {
        perror("Erreur allocation mémoire pour clients");
        dict_free(users_dict);
        dict_free(room_dict);
        exit(EXIT_FAILURE);
    }
    
    // Initialisation du tableau des clients
    for (int i = 0; i < MAX_USERS; i++) {
        clients[i].active = 0;
        clients[i].room_count = 0;
    }

    load_users_from_file("users.txt");
    load_rooms_from_file("rooms.txt");
    
    printf("Structures clients et salles créées\n");
    printf("En attente de connexions...\n");
    
    // Boucle principale
    char buffer[BUFFER_SIZE];
    struct sockaddr_in aE;
    socklen_t lgA = sizeof(struct sockaddr_in);
    
    while (1) {
        // Réception message
        memset(buffer, 0, BUFFER_SIZE);
        int recvLen = recvfrom(dS, buffer, BUFFER_SIZE-1, 0, (struct sockaddr*) &aE, &lgA);
        
        if (recvLen < 0) {
            perror("Erreur réception");
            continue;
        }
        
        buffer[recvLen] = '\0';
        
        // Extraire l'adresse IP de l'expéditeur pour l'affichage
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(aE.sin_addr), client_ip, INET_ADDRSTRLEN);
        
        printf("Message reçu de %s:%d: %s\n", client_ip, ntohs(aE.sin_port), buffer);
        
        // Vérifier si c'est une commande login
        if (strncmp(buffer, LOGIN_CMD, strlen(LOGIN_CMD)) == 0) {
            // Format attendu: "@login username"
            char *username = buffer + strlen(LOGIN_CMD) + 1; // +1 pour l'espace
            
            // Vérifier si le username est valide (non vide)
            if (strlen(username) > 0) {
                // Vérifier si l'utilisateur existe déjà
                const char* existing_index_str = dict_get(users_dict, username);
                
                if (existing_index_str != NULL) {
                    // L'utilisateur existe déjà dans notre dictionnaire
                    int existing_index = atoi(existing_index_str);
                    
                    if (existing_index >= 0 && existing_index < client_count) {
                        if (clients[existing_index].active) {
                            // L'utilisateur est déjà connecté
                            printf("Utilisateur déjà connecté: %s\n", username);
                            
                            // Envoyer message d'erreur
                            char response[BUFFER_SIZE];
                            sprintf(response, "Erreur: Nom d'utilisateur '%s' déjà utilisé.", username);
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        } else {
                            // Réactiver l'utilisateur existant
                            clients[existing_index].active = 1;
                            clients[existing_index].addr = aE;  // Mettre à jour l'adresse
                            
                            char client_ip[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &(aE.sin_addr), client_ip, INET_ADDRSTRLEN);
                            printf("Utilisateur réactivé: %s @ %s:%d (index: %d)\n", 
                                username, client_ip, ntohs(aE.sin_port), existing_index);
                            
                            // Envoyer confirmation
                            char response[BUFFER_SIZE];
                            sprintf(response, "Bienvenue de retour %s! Vous êtes connecté.", username);
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        }
                    } else {
                        // Index invalide, traiter comme nouvel utilisateur
                        // (ne devrait pas arriver, mais au cas où)
                        handle_new_user_login(username, aE, lgA);
                    }
                } else {
                    // Nouvel utilisateur
                    handle_new_user_login(username, aE, lgA);
                }
            } else {
                // Username vide
                char response[BUFFER_SIZE] = "Erreur: Veuillez fournir un nom d'utilisateur valide.";
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            }
        } 
        // Vérifier si c'est un message à rediriger
        else if (strncmp(buffer, MESSAGE_CMD, strlen(MESSAGE_CMD)) == 0) {
            // Format attendu: "@message &destinataire message"
            char dest_username[BUFFER_SIZE] = {0};
            char message_content[BUFFER_SIZE] = {0};
            
            // Extraire le destinataire et le contenu du message
            char *message_start = buffer + strlen(MESSAGE_CMD) + 1; // +1 pour l'espace
            
            // Trouver la position de l'esperluette (&) et de l'espace qui suit le nom du destinataire
            char *ampersand = strchr(message_start, '&');
            
            if (ampersand) {
                // Pointeur au début du nom du destinataire (juste après &)
                char *dest_start = ampersand + 1;
                
                // Chercher le premier espace après le nom du destinataire
                char *space_after_dest = strchr(dest_start, ' ');
                
                if (space_after_dest) {
                    // Calculer la longueur du nom du destinataire
                    size_t dest_len = space_after_dest - dest_start;
                    
                    if (dest_len > 0 && dest_len < BUFFER_SIZE) {
                        // Extraire le nom du destinataire
                        strncpy(dest_username, dest_start, dest_len);
                        dest_username[dest_len] = '\0';
                        
                        // Extraire le contenu du message (après l'espace qui suit le nom du destinataire)
                        strncpy(message_content, space_after_dest + 1, BUFFER_SIZE - 1);
                        
                        // Chercher l'index du destinataire dans le dictionnaire
                        const char *dest_index_str = dict_get(users_dict, dest_username);
                        
                        if (dest_index_str) {
                            int dest_index = atoi(dest_index_str);
                            
                            // Vérifier que l'index est valide et que le client est actif
                            if (dest_index >= 0 && dest_index < client_count && clients[dest_index].active) {
                                // Récupérer l'expéditeur
                                char sender_username[BUFFER_SIZE] = "inconnu";
                                
                                // Chercher l'expéditeur parmi les clients connectés
                                for (int i = 0; i < client_count; i++) {
                                    if (clients[i].active && 
                                        clients[i].addr.sin_addr.s_addr == aE.sin_addr.s_addr && 
                                        clients[i].addr.sin_port == aE.sin_port) {
                                        strncpy(sender_username, clients[i].username, BUFFER_SIZE - 1);
                                        break;
                                    }
                                }
                                
                                // Construire le message à transférer
                                char forward_msg[BUFFER_SIZE];
                                sprintf(forward_msg, "Message de %s: %s", sender_username, message_content);
                                
                                // Récupérer l'adresse complète du destinataire
                                struct sockaddr_in dest_addr = clients[dest_index].addr;
                                
                                // Envoyer le message au destinataire
                                if (sendto(dS, forward_msg, strlen(forward_msg), 0, 
                                         (struct sockaddr*) &dest_addr, sizeof(dest_addr)) < 0) {
                                    perror("Erreur envoi message");
                                    
                                    // Informer l'expéditeur de l'échec
                                    char error_msg[BUFFER_SIZE];
                                    sprintf(error_msg, "Erreur d'envoi du message à %s.", dest_username);
                                    sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                                } else {
                                    char dest_ip[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &(dest_addr.sin_addr), dest_ip, INET_ADDRSTRLEN);
                                    printf("Message redirigé à %s (%s:%d)\n", dest_username, 
                                          dest_ip, ntohs(dest_addr.sin_port));
                                    
                                    // Confirmer l'envoi à l'expéditeur
                                    char confirm_msg[BUFFER_SIZE];
                                    sprintf(confirm_msg, "Message envoyé à %s.", dest_username);
                                    sendto(dS, confirm_msg, strlen(confirm_msg), 0, (struct sockaddr*) &aE, lgA);
                                }
                            } else {
                                // Destinataire inactif ou index invalide
                                char error_msg[BUFFER_SIZE];
                                sprintf(error_msg, "Erreur: Utilisateur '%s' n'est plus connecté.", dest_username);
                                sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                            }
                        } else {
                            // Destinataire introuvable
                            char error_msg[BUFFER_SIZE];
                            sprintf(error_msg, "Erreur: Utilisateur '%s' introuvable.", dest_username);
                            sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                        }
                    } else {
                        // Nom de destinataire invalide
                        char error_msg[BUFFER_SIZE] = "Erreur: Format de destinataire invalide.";
                        sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                    }
                } else {
                    // Pas d'espace après le destinataire
                    char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                    sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                }
            } else {
                // Format de message invalide, pas d'esperluette
                char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
            }
        }
        // Commande pour créer une salle
        else if (strncmp(buffer, CREATEROOM_CMD, strlen(CREATEROOM_CMD)) == 0) {
            // Format attendu: "@createroom nom_salle max_membres"
            char *params = buffer + strlen(CREATEROOM_CMD) + 1; // +1 pour l'espace
            
            char room_name[MAX_ROOM_NAME_LENGTH] = {0};
            int max_members = 10; // Valeur par défaut
            
            // Extraire le nom de la salle et le nombre max de membres
            if (sscanf(params, "%s %d", room_name, &max_members) >= 1) {
                // Vérifier que max_members est au moins 1
                if (max_members <= 0) {
                    char response[BUFFER_SIZE] = "Erreur: Le nombre maximum de membres doit être au moins 1.";
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    continue;
                }
                
                // Vérifier si la salle existe déjà
                if (dict_get(room_dict, room_name) != NULL) {
                    char response[BUFFER_SIZE];
                    sprintf(response, "Erreur: Une salle nommée '%s' existe déjà.", room_name);
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else if (room_count >= MAX_ROOMS) {
                    // Nombre maximum de salles atteint
                    char response[BUFFER_SIZE] = "Erreur: Nombre maximum de salles atteint.";
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else {
                    // Créer la nouvelle salle
                    ChatRoom *new_room = chatroom_create(room_name, max_members);
                    if (!new_room) {
                        char response[BUFFER_SIZE] = "Erreur: Impossible de créer la salle.";
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    } else {
                        // Ajouter la salle au tableau et au dictionnaire
                        rooms[room_count] = new_room;
                        
                        char index_str[10];
                        sprintf(index_str, "%d", room_count);
                        dict_insert(room_dict, room_name, index_str);
                        
                        // Trouver le client qui a créé la salle
                        int client_index = find_client_index(&aE);
                        if (client_index >= 0) {
                            // Ajouter le créateur comme premier membre
                            chatroom_add_member(new_room, client_index);
                            
                            // Ajouter la salle à la liste des salles du client
                            if (clients[client_index].room_count < MAX_ROOMS) {
                                clients[client_index].joined_rooms[clients[client_index].room_count++] = room_count;
                            }
                            
                            char response[BUFFER_SIZE];
                            sprintf(response, "Salle '%s' créée avec succès et vous y avez été ajouté.", room_name);
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        } else {
                            char response[BUFFER_SIZE];
                            sprintf(response, "Salle '%s' créée avec succès.", room_name);
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        }
                        
                        room_count++;
                    }
                }
            } else {
                // Format invalide
                char response[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@createroom nom_salle max_membres'.";
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            }
        }
        // Commande pour rejoindre une salle
        else if (strncmp(buffer, JOINROOM_CMD, strlen(JOINROOM_CMD)) == 0) {
            // Format attendu: "@joinroom nom_salle"
            char *room_name = buffer + strlen(JOINROOM_CMD) + 1; // +1 pour l'espace
            
            // Chercher la salle
            int room_index = find_room_by_name(room_name);
            if (room_index < 0) {
                char response[BUFFER_SIZE];
                sprintf(response, "Erreur: Salle '%s' introuvable.", room_name);
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            } else {
                // Trouver le client qui souhaite rejoindre
                int client_index = find_client_index(&aE);
                if (client_index < 0) {
                    char response[BUFFER_SIZE] = "Erreur: Vous n'êtes pas connecté.";
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else {
                    // Vérifier si le client est déjà membre
                    if (chatroom_is_member(rooms[room_index], client_index)) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Vous êtes déjà membre de la salle '%s'.", room_name);
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    } else if (chatroom_is_full(rooms[room_index])) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Erreur: La salle '%s' est pleine.", room_name);
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    } else {
                        // Ajouter le client à la salle
                        chatroom_add_member(rooms[room_index], client_index);
                        
                        // Ajouter la salle à la liste des salles du client
                        if (clients[client_index].room_count < MAX_ROOMS) {
                            clients[client_index].joined_rooms[clients[client_index].room_count++] = room_index;
                            
                            char response[BUFFER_SIZE];
                            sprintf(response, "Vous avez rejoint la salle '%s'.", room_name);
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                            
                            // Notifier les autres membres
                            char notification[BUFFER_SIZE];
                            sprintf(notification, "%s a rejoint la salle.", clients[client_index].username);
                            broadcast_to_room(room_index, notification, "Serveur", &aE);
                        } else {
                            chatroom_remove_member(rooms[room_index], client_index);
                            
                            char response[BUFFER_SIZE] = "Erreur: Vous avez rejoint trop de salles.";
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        }
                    }
                }
            }
        }
        // Commande pour quitter une salle
        else if (strncmp(buffer, LEAVEROOM_CMD, strlen(LEAVEROOM_CMD)) == 0) {
            // Format attendu: "@leaveroom nom_salle"
            char *room_name = buffer + strlen(LEAVEROOM_CMD) + 1; // +1 pour l'espace
            
            // Chercher la salle
            int room_index = find_room_by_name(room_name);
            if (room_index < 0) {
                char response[BUFFER_SIZE];
                sprintf(response, "Erreur: Salle '%s' introuvable.", room_name);
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            } else {
                // Trouver le client qui souhaite quitter
                int client_index = find_client_index(&aE);
                if (client_index < 0) {
                    char response[BUFFER_SIZE] = "Erreur: Vous n'êtes pas connecté.";
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else {
                    // Vérifier si le client est membre
                    if (!chatroom_is_member(rooms[room_index], client_index)) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Erreur: Vous n'êtes pas membre de la salle '%s'.", room_name);
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    } else {
                        // Retirer le client de la salle
                        chatroom_remove_member(rooms[room_index], client_index);
                        
                        // Retirer la salle de la liste des salles du client
                        for (int i = 0; i < clients[client_index].room_count; i++) {
                            if (clients[client_index].joined_rooms[i] == room_index) {
                                // Décaler les éléments suivants
                                for (int j = i; j < clients[client_index].room_count - 1; j++) {
                                    clients[client_index].joined_rooms[j] = clients[client_index].joined_rooms[j + 1];
                                }
                                clients[client_index].room_count--;
                                break;
                            }
                        }
                        
                        char response[BUFFER_SIZE];
                        sprintf(response, "Vous avez quitté la salle '%s'.", room_name);
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        
                        // Notifier les autres membres
                        char notification[BUFFER_SIZE];
                        sprintf(notification, "%s a quitté la salle.", clients[client_index].username);
                        broadcast_to_room(room_index, notification, "Serveur", &aE);
                    }
                }
            }
        }
        // Commande pour lister les salles
        else if (strncmp(buffer, LISTROOMS_CMD, strlen(LISTROOMS_CMD)) == 0) {
            if (room_count == 0) {
                char response[BUFFER_SIZE] = "Aucune salle n'existe actuellement.";
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            } else {
                char response[BUFFER_SIZE] = "Liste des salles disponibles:\n";
                
                for (int i = 0; i < room_count; i++) {
                    if (rooms[i] && rooms[i]->active) {
                        char room_info[100];
                        sprintf(room_info, "%s (%d/%d membres)\n", 
                                rooms[i]->name, 
                                chatroom_get_member_count(rooms[i]), 
                                chatroom_get_max_members(rooms[i]));
                        
                        // S'assurer qu'il y a assez d'espace dans la réponse
                        if (strlen(response) + strlen(room_info) < BUFFER_SIZE - 1) {
                            strcat(response, room_info);
                        }
                    }
                }
                
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            }
        }
        // Commande pour lister les membres d'une salle
        else if (strncmp(buffer, LISTMEMBERS_CMD, strlen(LISTMEMBERS_CMD)) == 0) {
            // Format attendu: "@listmembers nom_salle"
            char *room_name = buffer + strlen(LISTMEMBERS_CMD) + 1; // +1 pour l'espace
            
            // Chercher la salle
            int room_index = find_room_by_name(room_name);
            if (room_index < 0) {
                char response[BUFFER_SIZE];
                sprintf(response, "Erreur: Salle '%s' introuvable.", room_name);
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            } else {
                ChatRoom *room = rooms[room_index];
                
                if (chatroom_get_member_count(room) == 0) {
                    char response[BUFFER_SIZE];
                    sprintf(response, "La salle '%s' ne contient aucun membre.", room_name);
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else {
                    char response[BUFFER_SIZE];
                    sprintf(response, "Membres de la salle '%s':\n", room_name);
                    
                    for (int i = 0; i < room->member_count; i++) {
                        int member_index = room->member_indices[i];
                        
                        char member_info[100];
                        sprintf(member_info, "- %s\n", clients[member_index].username);
                        
                        // S'assurer qu'il y a assez d'espace dans la réponse
                        if (strlen(response) + strlen(member_info) < BUFFER_SIZE - 1) {
                            strcat(response, member_info);
                        }
                    }
                    
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                }
            }
        }
        // Commande pour envoyer un message à une salle
        else if (strncmp(buffer, ROOMSG_CMD, strlen(ROOMSG_CMD)) == 0) {
            // Format attendu: "@roomsg nom_salle message"
            char *params = buffer + strlen(ROOMSG_CMD) + 1; // +1 pour l'espace
            
            // Extraire le nom de la salle et le message
            char room_name[MAX_ROOM_NAME_LENGTH] = {0};
            char *message_content = NULL;
            
            // Trouver le premier espace après le nom de la salle
            char *space_after_room = strchr(params, ' ');
            if (space_after_room) {
                size_t room_name_len = space_after_room - params;
                
                if (room_name_len > 0 && room_name_len < MAX_ROOM_NAME_LENGTH) {
                    // Extraire le nom de la salle
                    strncpy(room_name, params, room_name_len);
                    room_name[room_name_len] = '\0';
                    
                    // Extraire le contenu du message
                    message_content = space_after_room + 1;
                    
                    // Chercher la salle
                    int room_index = find_room_by_name(room_name);
                    if (room_index < 0) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Erreur: Salle '%s' introuvable.", room_name);
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    } else {
                        // Trouver le client qui envoie le message
                        int client_index = find_client_index(&aE);
                        if (client_index < 0) {
                            char response[BUFFER_SIZE] = "Erreur: Vous n'êtes pas connecté.";
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        } else {
                            // Vérifier si le client est membre de la salle
                            if (!chatroom_is_member(rooms[room_index], client_index)) {
                                char response[BUFFER_SIZE];
                                sprintf(response, "Erreur: Vous n'êtes pas membre de la salle '%s'.", room_name);
                                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                            } else {
                                // Diffuser le message à tous les membres de la salle
                                broadcast_to_room(room_index, message_content, clients[client_index].username, &aE);
                                
                                // Confirmer l'envoi
                                char confirm_msg[BUFFER_SIZE];
                                sprintf(confirm_msg, "Message envoyé à la salle '%s'.", room_name);
                                sendto(dS, confirm_msg, strlen(confirm_msg), 0, (struct sockaddr*) &aE, lgA);
                            }
                        }
                    }
                } else {
                    // Nom de salle invalide
                    char response[BUFFER_SIZE] = "Erreur: Nom de salle invalide.";
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                }
            } else {
                // Format invalide
                char response[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@roomsg nom_salle message'.";
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            }
        } else {
            // Message standard, format non reconnu
            printf("Message standard reçu\n");
            
            // Informer l'expéditeur que le format du message n'est pas reconnu
            char help_msg[BUFFER_SIZE] = 
                "Format non reconnu. Commandes disponibles:\n"
                "@login username - Se connecter\n"
                "@message &destinataire message - Envoyer un message privé\n"
                "@createroom nom_salle max_membres - Créer une salle\n"
                "@joinroom nom_salle - Rejoindre une salle\n"
                "@leaveroom nom_salle - Quitter une salle\n"
                "@listrooms - Lister les salles disponibles\n"
                "@listmembers nom_salle - Lister les membres d'une salle\n"
                "@roomsg nom_salle message - Envoyer un message à une salle";
            
            sendto(dS, help_msg, strlen(help_msg), 0, (struct sockaddr*) &aE, lgA);
        }
    }
    
    // Cette partie ne sera jamais atteinte en raison de la boucle infinie
    // mais elle est incluse pour compléter le code
    
    // Libération des ressources
    for (int i = 0; i < room_count; i++) {
        chatroom_free(rooms[i]);
    }
    dict_free(users_dict);
    dict_free(room_dict);
    free(clients);
    close(dS);
    
    return 0;
}