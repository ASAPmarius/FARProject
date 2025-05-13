#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "globalVariables.h"
#include "dict.h"

#define BUFFER_SIZE 1000
#define LOGIN_CMD "@login"  //format: "@login username password"
#define MESSAGE_CMD "@message" //format: "@message &destinataire message"
#define USERS_FILE "save_users.txt"
#define MAX_PASSWORD_LENGTH 50

// Structure pour stocker les informations utilisateur
typedef struct {
    char username[BUFFER_SIZE];
    char password[MAX_PASSWORD_LENGTH];
} userAuthentification;

// Fonction pour vérifier si un utilisateur existe et vérifier son mot de passe
bool check_user_credentials(const char* username, const char* password) {
    FILE* file = fopen(USERS_FILE, "r");
    if (!file) {
        return false;
    }

    userAuthentification user;
    bool found = false;
    while (fscanf(file, "%s %s", user.username, user.password) == 2) {
        if (strcmp(user.username, username) == 0) {
            found = true;
            if (strcmp(user.password, password) == 0) {
                fclose(file);
                return true;
            }
            break;
        }
    }
    fclose(file);
    return false;
}

// Fonction pour sauvegarder un nouvel utilisateur
void save_user_credentials(const char* username, const char* password) {
    FILE* file = fopen(USERS_FILE, "a");
    if (file) {
        fprintf(file, "%s %s\n", username, password);
        fclose(file);
    }
}

// Fonction pour vérifier si un utilisateur existe déjà
bool user_exists(const char* username) {
    FILE* file = fopen(USERS_FILE, "r");
    if (!file) {
        return false;
    }

    userAuthentification user;
    while (fscanf(file, "%s %s", user.username, user.password) == 2) {
        if (strcmp(user.username, username) == 0) {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

int main(int argc, char *argv[]) {
    printf("Début programme récepteur UDP\n");

    // Création de la socket UDP
    int dS = socket(PF_INET, SOCK_DGRAM, 0);
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
    printf("Socket Nommée\n");

    // Création du dictionnaire des utilisateurs
    SimpleDict *users = dict_create();
    if (!users) {
        perror("Erreur création dictionnaire");
        exit(EXIT_FAILURE);
    }
    printf("Dictionnaire utilisateurs créé\n");

    // Boucle principale
    char buffer[BUFFER_SIZE];
    struct sockaddr_in aE;
    socklen_t lgA = sizeof(struct sockaddr_in);
    
    printf("En attente de connexions...\n");
    
    while (1) {
        // Réception message
        memset(buffer, 0, BUFFER_SIZE);
        int recvLen = recvfrom(dS, buffer, BUFFER_SIZE-1, 0, (struct sockaddr*) &aE, &lgA);
        
        if (recvLen < 0) {
            perror("Erreur réception");
            continue;
        }
        
        buffer[recvLen] = '\0';
        
        // Extraire l'adresse IP de l'expéditeur
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(aE.sin_addr), client_ip, INET_ADDRSTRLEN);
        
        printf("Message reçu de %s: %s\n", client_ip, buffer);
          // Vérifier si c'est une commande login
        if (strncmp(buffer, LOGIN_CMD, strlen(LOGIN_CMD)) == 0) {
            char username[BUFFER_SIZE] = {0};
            char password[MAX_PASSWORD_LENGTH] = {0};
            
            // Format attendu: "@login username password"
            if (sscanf(buffer + strlen(LOGIN_CMD) + 1, "%s %s", username, password) >= 1) {
                if (strlen(username) > 0) {
                    if (user_exists(username)) {
                        // Utilisateur existe, vérifier le mot de passe
                        if (check_user_credentials(username, password)) {
                            if (dict_insert(users, username, client_ip)) {
                                printf("Utilisateur connecté: %s @ %s\n", username, client_ip);
                                char response[BUFFER_SIZE];
                                sprintf(response, "Bienvenue %s! Vous êtes connecté.", username);
                                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                            }
                        } else {
                            // Mot de passe incorrect
                            printf("Tentative de connexion échouée pour %s: mot de passe incorrect\n", username);
                            char response[BUFFER_SIZE] = "Erreur: Mot de passe incorrect. Veuillez réessayer.";
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        }
                    } else {
                        // Nouvel utilisateur
                        if (strlen(password) > 0) {
                            save_user_credentials(username, password);
                            if (dict_insert(users, username, client_ip)) {
                                printf("Nouvel utilisateur enregistré: %s @ %s\n", username, client_ip);
                                char response[BUFFER_SIZE];
                                sprintf(response, "Bienvenue %s! Votre compte a été créé et vous êtes connecté.", username);
                                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                            }
                        } else {
                            char response[BUFFER_SIZE] = "Erreur: Veuillez fournir un mot de passe pour créer votre compte.";
                            sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        }
                    }
                } else {
                    // Username vide
                    char response[BUFFER_SIZE] = "Erreur: Veuillez fournir un nom d'utilisateur valide.";
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                }
            } else {
                char response[BUFFER_SIZE] = "Format incorrect. Utilisez: @login username password";
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            }
        } 
        // Vérifier si c'est un message à rediriger
        else if (strncmp(buffer, MESSAGE_CMD, strlen(MESSAGE_CMD)) == 0) {
            // Format attendu: "@message (destinataire) (contenu du message)"
            char dest_username[BUFFER_SIZE] = {0};
            char message_content[BUFFER_SIZE] = {0};
            
            // Extraire le destinataire et le contenu du message
            char *message_start = buffer + strlen(MESSAGE_CMD) + 1; // +1 pour l'espace
            
            // Trouver la position des parenthèses
            char *open_paren = strchr(message_start, '(');
            char *close_paren = strchr(message_start, ')');
            
            if (open_paren && close_paren && open_paren < close_paren) {
                // Extraire le nom du destinataire entre parenthèses
                size_t dest_len = close_paren - open_paren - 1;
                if (dest_len > 0 && dest_len < BUFFER_SIZE) {
                    strncpy(dest_username, open_paren + 1, dest_len);
                    dest_username[dest_len] = '\0';
                    
                    // Extraire le contenu du message après la parenthèse fermante
                    char *content = close_paren + 1;
                    if (*content == ' ') content++; // Ignorer l'espace après la parenthèse
                    strncpy(message_content, content, BUFFER_SIZE - 1);
                    
                    // Chercher l'adresse IP du destinataire
                    const char *dest_ip = dict_get(users, dest_username);
                    
                    if (dest_ip) {
                        // Récupérer l'expéditeur (pourrait être stocké dans une autre structure)
                        char sender_username[BUFFER_SIZE] = "inconnu";
                        
                        // Parcourir le dictionnaire pour trouver le nom d'utilisateur associé à l'IP
                        for (size_t i = 0; i < users->count; i++) {
                            if (strcmp(users->entries[i].value, client_ip) == 0) {
                                strncpy(sender_username, users->entries[i].key, BUFFER_SIZE - 1);
                                break;
                            }
                        }
                        
                        // Construire le message à transférer
                        char forward_msg[BUFFER_SIZE];
                        sprintf(forward_msg, "Message de %s: %s", sender_username, message_content);
                        
                        // Configurer l'adresse du destinataire
                        struct sockaddr_in dest_addr;
                        dest_addr.sin_family = AF_INET;
                        dest_addr.sin_port = htons(serverPort); // On suppose que tous les clients utilisent le même port
                        inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr);
                        
                        // Envoyer le message au destinataire
                        if (sendto(dS, forward_msg, strlen(forward_msg), 0, 
                                 (struct sockaddr*) &dest_addr, sizeof(dest_addr)) < 0) {
                            perror("Erreur envoi message");
                            
                            // Informer l'expéditeur de l'échec
                            char error_msg[BUFFER_SIZE];
                            sprintf(error_msg, "Erreur d'envoi du message à %s.", dest_username);
                            sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                        } else {
                            printf("Message redirigé à %s (%s)\n", dest_username, dest_ip);
                            
                            // Confirmer l'envoi à l'expéditeur
                            char confirm_msg[BUFFER_SIZE];
                            sprintf(confirm_msg, "Message envoyé à %s.", dest_username);
                            sendto(dS, confirm_msg, strlen(confirm_msg), 0, (struct sockaddr*) &aE, lgA);
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
                // Format de message invalide
                char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message (destinataire) message'.";
                sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
            }
        } else {
            // Autre type de message, on pourrait vérifier si l'IP est associée à un utilisateur
            // et traiter en conséquence
            printf("Message standard reçu\n");
            
            // Informer l'expéditeur que le format du message n'est pas reconnu
            char help_msg[BUFFER_SIZE] = "Format non reconnu. Utilisez 'login username' pour vous connecter ou '@message (destinataire) message' pour envoyer un message.";
            sendto(dS, help_msg, strlen(help_msg), 0, (struct sockaddr*) &aE, lgA);
        }
    }
    
    // Libération des ressources
    dict_free(users);
    close(dS);
    
    return 0;
}
