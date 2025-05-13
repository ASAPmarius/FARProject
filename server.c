#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "globalVariables.h"
#include "dict.h"
#include <signal.h>

#define BUFFER_SIZE 1000
#define LOGIN_CMD "@login"  //format: "@login username"
#define MESSAGE_CMD "@message" //format: "@message &destinataire message"
#define MAX_USERS 100  // Maximum d'utilisateurs simultanés

static volatile sig_atomic_t running = 1;

// Handler pour SIGINT ou @shutdown
void handle_sigint(int sig) {
    running = 0;
}

// Structure pour stocker les informations complètes d'un client
typedef struct {
    char username[50];               // Nom d'utilisateur
    struct sockaddr_in addr;         // Structure d'adresse complète
    int active;                      // Flag pour indiquer si l'entrée est active
} ClientInfo;



int main(int argc, char *argv[]) {

    // installer le handler
    signal(SIGINT, handle_sigint);

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
    
    while (running) {
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
            // Extraire le nom d'utilisateur et le mot de passe
            char *username = strtok(buffer + strlen(LOGIN_CMD) + 1, " ");
            char *password = strtok(NULL, " ");

            if (username && password) {
                int clientIndex = -1;
                // Chercher si l'utilisateur est déjà connecté dans le tableau des clients
                for (int i = 0; i < client_count; i++) {
                    if (strcmp(clients[i].username, username) == 0) {
                        clientIndex = i;
                        break;
                    }
                }

                // Vérifier si l'utilisateur existe déjà dans le dictionnaire (ses identifiants sont enregistrés)
                const char *stored_password = dict_get(users_dict, username);
                if (stored_password) {
                    if (strcmp(stored_password, password) == 0) {
                        // Authentification réussie
                        printf("Utilisateur authentifié: %s\n", username);
                        
                        // Si l'utilisateur n'est pas déjà dans le tableau (ou s'il s'est déconnecté), on l'ajoute ou on met à jour son adresse
                        if (clientIndex == -1) {
                            strncpy(clients[client_count].username, username, sizeof(clients[client_count].username) - 1);
                            clients[client_count].addr = aE;
                            clients[client_count].active = 1;
                            clientIndex = client_count;
                            client_count++;
                        } else {
                            // Met à jour l'adresse en cas de changement
                            clients[clientIndex].addr = aE;
                            clients[clientIndex].active = 1;
                        }
                        
                        char response[BUFFER_SIZE];
                        sprintf(response, "Bienvenue %s! Vous êtes connecté.", username);
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    } else {
                        // Mot de passe incorrect
                        printf("Mot de passe incorrect pour l'utilisateur: %s\n", username);
                        char response[BUFFER_SIZE] = "Erreur: Mot de passe incorrect.";
                        sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    }
                } else {
                    // Nouvel utilisateur - ajouter au dictionnaire et au tableau des clients
                    dict_insert(users_dict, username, password);
                    printf("Nouvel utilisateur enregistré: %s\n", username);
                    
                    strncpy(clients[client_count].username, username, sizeof(clients[client_count].username) - 1);
                    clients[client_count].addr = aE;
                    clients[client_count].active = 1;
                    clientIndex = client_count;
                    client_count++;
                    
                    char response[BUFFER_SIZE];
                    sprintf(response, "Bienvenue %s! Vous êtes enregistré et connecté.", username);
                    sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                }
            } else {
                // Informations manquantes
                char response[BUFFER_SIZE] = "Erreur: Veuillez fournir un nom d'utilisateur et un mot de passe.";
                sendto(dS, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
            }
        } 

        //commande ping 
        else if (strncmp(buffer, "@ping", 5) == 0) {
            const char *reply = "pong\n";
            if (sendto(dS, reply, strlen(reply), 0,
                    (struct sockaddr*)&aE, lgA) < 0) {
                perror("sendto @ping");
            }
            // on continue la boucle sans tomber dans les autres branches
            continue;
        }

        //commande @shutdown
        else if (strncmp(buffer, "@shutdown", 9) == 0) {
            // Acknowledge
            const char *msg = "Serveur éteint!\n";
            sendto(dS, msg, strlen(msg), 0,
                (struct sockaddr*)&aE, lgA);

            // Déclenche le handler SIGINT => running = 0
            raise(SIGINT);
            // on sortira proprement de la boucle
            continue;
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
                                            
                        // Recherche de l'indice du destinataire dans clients[]
                        int dest_index = -1;
                        for (int j = 0; j < client_count; j++) {
                            if (clients[j].active &&
                                strcmp(clients[j].username, dest_username) == 0) {
                                dest_index = j;
                                break;
                            }
                        }

                        if (dest_index < 0) {
                            // Destinataire introuvable ou déconnecté
                            char err[BUFFER_SIZE];
                            snprintf(err, sizeof(err),
                                    "Erreur: Utilisateur '%s' introuvable ou déconnecté.\n",
                                    dest_username);
                            sendto(dS, err, strlen(err), 0,
                                (struct sockaddr*)&aE, lgA);
                            continue;
                        }

                        // À partir d'ici dest_index est valide
                        // on récupère l'expéditeur
                        char sender_username[BUFFER_SIZE] = "inconnu";
                        for (int i = 0; i < client_count; i++) {
                            if (clients[i].active &&
                                clients[i].addr.sin_addr.s_addr == aE.sin_addr.s_addr &&
                                clients[i].addr.sin_port        == aE.sin_port) {
                                strncpy(sender_username,
                                        clients[i].username,
                                        sizeof(sender_username)-1);
                                break;
                            }
                        }

                        // Forward du message
                        char forward_msg[BUFFER_SIZE];
                        snprintf(forward_msg, sizeof(forward_msg),
                                "Message de %s: %s\n",
                                sender_username, message_content);

                        struct sockaddr_in *dest_addr = &clients[dest_index].addr;
                        if (sendto(dS, forward_msg, strlen(forward_msg), 0,
                                (struct sockaddr*)dest_addr, sizeof(*dest_addr)) < 0) {
                            perror("sendto @message");
                        } else {
                            char conf[BUFFER_SIZE];
                            snprintf(conf, sizeof(conf),
                                    "Message envoyé à %s.\n", dest_username);
                            sendto(dS, conf, strlen(conf), 0,
                                (struct sockaddr*)&aE, lgA);
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