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
            // Format attendu: "login username"
            char *username = buffer + strlen(LOGIN_CMD) + 1; // +1 pour l'espace
            
            // Vérifier si le username est valide (non vide)
            if (strlen(username) > 0) {
                // Insérer ou mettre à jour dans le dictionnaire
                if (dict_insert(users, username, client_ip)) {
                    printf("Nouvel utilisateur enregistré: %s @ %s\n", username, client_ip);
                    
                    // Envoyer confirmation
                    char response[BUFFER_SIZE];
                    sprintf(response, "Bienvenue %s! Vous êtes connecté.", username);
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                } else {
                    // L'utilisateur existe déjà, mise à jour impossible avec notre implémentation
                    printf("Utilisateur déjà existant: %s\n", username);
                    
                    // Envoyer message d'erreur
                    char response[BUFFER_SIZE];
                    sprintf(response, "Erreur: Nom d'utilisateur '%s' déjà utilisé.", username);
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
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
            
            // Trouver la position du symbole &
            char *amp_symbol = strchr(message_start, '&');
            
            if (amp_symbol) {
                // Extraire le nom du destinataire (commence après le & jusqu'au prochain espace)
                char *dest_start = amp_symbol + 1; // Sauter le &
                char *space_after_dest = strchr(dest_start, ' ');
                
                if (space_after_dest) {
                    // Calculer la longueur du nom de destinataire
                    size_t dest_len = space_after_dest - dest_start;
                    
                    if (dest_len > 0 && dest_len < BUFFER_SIZE) {
                        // Copier le nom du destinataire
                        strncpy(dest_username, dest_start, dest_len);
                        dest_username[dest_len] = '\0';
                        
                        // Extraire le contenu du message (tout ce qui suit l'espace après le destinataire)
                        char *content = space_after_dest + 1;
                        strncpy(message_content, content, BUFFER_SIZE - 1);
                        
                        // Chercher l'adresse IP du destinataire
                        const char *dest_ip = dict_get(users, dest_username);
                        
                        if (dest_ip) {
                            // ... reste du code pour envoyer le message ...
                            // (cette partie reste inchangée)
                            
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
                // Symbole & manquant
                char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
            }
        } else {
            // Autre type de message
            printf("Message standard reçu\n");
            
            // Mettre à jour le message d'aide
            char help_msg[BUFFER_SIZE] = "Format non reconnu. Utilisez '@login username' pour vous connecter ou '@message &destinataire message' pour envoyer un message.";
            sendto(dS, help_msg, strlen(help_msg), 0, (struct sockaddr*) &aE, lgA);
        }
    }
    
    // Libération des ressources
    dict_free(users);
    close(dS);
    
    return 0;
}
