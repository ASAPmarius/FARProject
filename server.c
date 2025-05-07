#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "globalVariables.h"
#include "dict.h"

#define BUFFER_SIZE 1000
#define LOGIN_CMD "@login"  //format: "@login username"
#define MESSAGE_CMD "@message" //format: "@message &destinataire message"
#define MAX_USERS 100  // Maximum d'utilisateurs simultanés

// Structure pour stocker les informations complètes d'un client
typedef struct {
    char username[50];               // Nom d'utilisateur
    struct sockaddr_in addr;         // Structure d'adresse complète
    int active;                      // Flag pour indiquer si l'entrée est active
} ClientInfo;

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
    printf("Socket Nommée sur port %d\n", serverPort);

    // Création du dictionnaire des utilisateurs (pour les recherches par nom)
    SimpleDict *users_dict = dict_create();
    if (!users_dict) {
        perror("Erreur création dictionnaire");
        exit(EXIT_FAILURE);
    }
    
    // Création du tableau des informations clients
    ClientInfo *clients = malloc(MAX_USERS * sizeof(ClientInfo));
    if (!clients) {
        perror("Erreur allocation mémoire pour clients");
        dict_free(users_dict);
        exit(EXIT_FAILURE);
    }
    
    // Initialisation du tableau des clients
    for (int i = 0; i < MAX_USERS; i++) {
        clients[i].active = 0;
    }
    
    int client_count = 0;
    printf("Structures clients créées\n");
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
                if (dict_get(users_dict, username) != NULL) {
                    // L'utilisateur existe déjà
                    printf("Utilisateur déjà existant: %s\n", username);
                    
                    // Envoyer message d'erreur
                    char response[BUFFER_SIZE];
                    sprintf(response, "Erreur: Nom d'utilisateur '%s' déjà utilisé.", username);
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else {
                    // Nouvel utilisateur - ajouter au dictionnaire avec un index dans le tableau
                    char index_str[10];
                    sprintf(index_str, "%d", client_count);
                    dict_insert(users_dict, username, index_str);
                    
                    // Stocker les informations complètes du client
                    if (client_count < MAX_USERS) {
                        strncpy(clients[client_count].username, username, sizeof(clients[client_count].username) - 1);
                        clients[client_count].addr = aE;  // Copie complète de la structure d'adresse
                        clients[client_count].active = 1;
                        
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
        } else {
            // Message standard, format non reconnu
            printf("Message standard reçu\n");
            
            // Informer l'expéditeur que le format du message n'est pas reconnu
            char help_msg[BUFFER_SIZE] = "Format non reconnu. Utilisez '@login username' pour vous connecter ou '@message &destinataire message' pour envoyer un message.";
            sendto(dS, help_msg, strlen(help_msg), 0, (struct sockaddr*) &aE, lgA);
        }
    }
    
    // Libération des ressources
    dict_free(users_dict);
    free(clients);
    close(dS);
    
    return 0;
}