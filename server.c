#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "globalVariables.h"
#include "dict.h"

#define BUFFER_SIZE 1000
#define LOGIN_CMD "login"  //-> format: "@login username"
#define MESSAGE_CMD "@message" //-> format: "@message &user message"

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
            // Format attendu: "@message &user message"
            char dest_username[BUFFER_SIZE] = {0};
            char message_content[BUFFER_SIZE] = {0};
            
            // Extraire le destinataire et le contenu du message
            char *cmd_end = buffer + strlen(MESSAGE_CMD); // Position juste après "@message"
            
            // Vérifier s'il y a un espace après la commande
            if (*cmd_end == ' ') {
                cmd_end++; // Avancer après l'espace
                
                // Chercher le symbole '&' qui précède le nom d'utilisateur
                char *user_start = strchr(cmd_end, '&');
                
                if (user_start) {
                    user_start++; // Avancer après le symbole '&'
                    
                    // Chercher le prochain espace qui marque la fin du nom d'utilisateur
                    char *user_end = strchr(user_start, ' ');
                    
                    if (user_end) {
                        // Extraire le nom d'utilisateur
                        size_t username_len = user_end - user_start;
                        strncpy(dest_username, user_start, username_len);
                        dest_username[username_len] = '\0';
                        
                        // Extraire le contenu du message (tout ce qui suit l'espace après le nom d'utilisateur)
                        size_t message_len = strlen(user_end + 1);
                        if (message_len > 0) {
                            strcpy(message_content, user_end + 1);
                            
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
                            // Message vide
                            char error_msg[BUFFER_SIZE] = "Erreur: Message vide.";
                            sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                        }
                    } else {
                        // Pas d'espace après le nom d'utilisateur
                        char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                        sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                    }
                } else {
                    // Pas de symbole '&' trouvé
                    char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                    sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                }
            } else {
                // Pas d'espace après @message
                char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                sendto(dS, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
            }
        } else {
            // Autre type de message, on pourrait vérifier si l'IP est associée à un utilisateur
            // et traiter en conséquence
            printf("Message standard reçu\n");
            
            // Informer l'expéditeur que le format du message n'est pas reconnu
            char help_msg[BUFFER_SIZE] = "Format non reconnu. Utilisez 'login username' pour vous connecter ou '@message &destinataire message' pour envoyer un message.";
            sendto(dS, help_msg, strlen(help_msg), 0, (struct sockaddr*) &aE, lgA);
        }
    }
    
    // Libération des ressources
    dict_free(users);
    close(dS);
    
    return 0;
}
