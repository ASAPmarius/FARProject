#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "globalVariables.h"
#include "dict.h"

#define BUFFER_SIZE 1000
#define FILE_BUFFER_SIZE 4096
#define LOGIN_CMD "@login"
#define MESSAGE_CMD "@message"
#define UPLOAD_CMD "@upload"
#define DOWNLOAD_CMD "@download"
#define TCP_PORT 8888
#define MAX_USERS 100

// Structure pour stocker les informations complètes d'un client
typedef struct {
    char username[50];               // Nom d'utilisateur
    struct sockaddr_in addr;         // Structure d'adresse complète
    int active;                      // Flag pour indiquer si l'entrée est active
} ClientInfo;

// Fonction pour créer et configurer la socket TCP
int setup_tcp_socket() {
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        perror("Erreur création socket TCP");
        return -1;
    }

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(TCP_PORT);

    // Permettre la réutilisation de l'adresse
    int opt = 1;
    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind avant listen
    if (bind(tcp_socket, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("Erreur bind socket TCP");
        close(tcp_socket);
        return -1;
    }

    if (listen(tcp_socket, 5) < 0) {
        perror("Erreur listen socket TCP");
        close(tcp_socket);
        return -1;
    }

    printf("Socket TCP écoutant sur le port %d\n", TCP_PORT);
    return tcp_socket;
}

// Fonction pour gérer l'upload d'un fichier
void handle_file_upload(int client_socket, const char* command) {
    // Extraire le nom du fichier de la commande
    const char* filename = command + strlen(UPLOAD_CMD) + 1;
    while (*filename == ' ') filename++; // Ignorer les espaces

    if (strlen(filename) == 0) {
        printf("Erreur: Nom de fichier manquant\n");
        return;
    }

    // Vérifier que le dossier uploads existe
    struct stat st = {0};
    if (stat("uploads", &st) == -1) {
        if (mkdir("uploads", 0777) == -1) {
            printf("Erreur: Impossible de créer le dossier uploads (errno: %d)\n", errno);
            perror("mkdir");
            return;
        }
        printf("Dossier uploads créé\n");
    }

    // Créer le chemin complet du fichier
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "uploads/%s", filename);
    printf("Réception du fichier: %s\n", filename);

    // Ouvrir le fichier en écriture
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        printf("Erreur: Création du fichier %s\n", filepath);
        return;
    }

    // Recevoir et écrire le contenu du fichier
    char buffer[FILE_BUFFER_SIZE];
    ssize_t bytes_received;
    size_t total_received = 0;

    while ((bytes_received = recv(client_socket, buffer, FILE_BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        total_received += bytes_received;
        printf("Réception en cours: %zu octets reçus\r", total_received);
        fflush(stdout);
    }

    printf("\nFichier %s reçu et sauvegardé (%zu octets)\n", filename, total_received);
    fclose(file);

    // Envoyer confirmation au client
    char confirm_msg[] = "FILE_RECEIVED_OK";
    send(client_socket, confirm_msg, strlen(confirm_msg), 0);
}

// Fonction pour gérer le téléchargement d'un fichier
void handle_file_download(int client_socket, const char* filename) {
    // Vérifier que le dossier uploads existe
    struct stat st = {0};
    if (stat("uploads", &st) == -1) {
        printf("Erreur: Le dossier uploads n'existe pas\n");
        char error_msg[] = "DIRECTORY_NOT_FOUND";
        send(client_socket, error_msg, strlen(error_msg), 0);
        return;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "uploads/%s", filename);
    printf("Tentative d'ouverture du fichier : %s\n", filepath);
    
    // Vérifier si le fichier existe et est accessible
    if (access(filepath, F_OK) == -1) {
        printf("Erreur: Le fichier %s n'existe pas\n", filepath);
        char error_msg[] = "FILE_NOT_FOUND";
        send(client_socket, error_msg, strlen(error_msg), 0);
        return;
    }

    // Ouvrir le fichier en lecture
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        printf("Erreur: Impossible d'ouvrir le fichier %s\n", filepath);
        char error_msg[] = "FILE_OPEN_ERROR";
        send(client_socket, error_msg, strlen(error_msg), 0);
        return;
    }

    // Obtenir la taille du fichier
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("Envoi du fichier %s (taille: %ld octets)\n", filename, file_size);

    //Envoyer un message de succès avant le contenu du fichier
    char success_msg[] = "FILE_SEND_START";
    send(client_socket, success_msg, strlen(success_msg), 0);

    // Petit délai pour s'assurer que le message de succès est bien reçu
    usleep(100000);  // 100ms

    // Envoyer le contenu du fichier
    char buffer[FILE_BUFFER_SIZE];
    size_t bytes_read;
    long total_sent = 0;

    while ((bytes_read = fread(buffer, 1, FILE_BUFFER_SIZE, file)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) < 0) {
            printf("Erreur lors de l'envoi du fichier\n");
            break;
        }
        total_sent += bytes_read;
        printf("Progression: %ld/%ld octets envoyés\r", total_sent, file_size);
        fflush(stdout);
    }

    printf("\nFichier %s envoyé (%ld octets)\n", filename, total_sent);
    fclose(file);
}

// Fonction pour gérer les transferts de fichiers
void handle_file_transfers(int tcp_socket) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        printf("En attente de connexion TCP...\n");
        int client_socket = accept(tcp_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        char command[BUFFER_SIZE];
        ssize_t recv_size = recv(client_socket, command, BUFFER_SIZE - 1, 0);
        if (recv_size > 0) {
            command[recv_size] = '\0';
            printf("Commande reçue : %s\n", command);
            
            if (strncmp(command, UPLOAD_CMD, strlen(UPLOAD_CMD)) == 0) {
                handle_file_upload(client_socket, command);
            } 
            else if (strncmp(command, DOWNLOAD_CMD, strlen(DOWNLOAD_CMD)) == 0) {
                char* filename = command + strlen(DOWNLOAD_CMD) + 1;
                while (*filename == ' ') filename++; // Ignorer les espaces
                if (strlen(filename) > 0) {
                    printf("Demande de téléchargement pour le fichier : %s\n", filename);
                    handle_file_download(client_socket, filename);
                }
            }
        }

        close(client_socket);
    }
}

// Variables globales pour les sockets
int dS_udp;  // Socket UDP pour la messagerie
int dS_tcp;  // Socket TCP pour les fichiers

int main(int argc, char *argv[]) {
    printf("Début programme serveur\n");

    // Création du dossier uploads s'il n'existe pas
    if (mkdir("uploads", 0777) == 0) {
        printf("Dossier 'uploads' créé avec succès\n");
    } else if (errno != EEXIST) {
        // Si l'erreur n'est pas que le dossier existe déjà
        perror("Erreur création dossier uploads");
        exit(EXIT_FAILURE);
    } else {
        printf("Dossier 'uploads' existe déjà\n");
    }

    // Création de la socket UDP
    dS_udp = socket(PF_INET, SOCK_DGRAM, 0);
    if (dS_udp == -1) {
        perror("Erreur création socket UDP");
        exit(EXIT_FAILURE);
    }
    printf("Socket UDP Créée\n");

    // Création et configuration de la socket TCP
    dS_tcp = setup_tcp_socket();
    if (dS_tcp == -1) {
        close(dS_udp);
        exit(EXIT_FAILURE);
    }
    printf("Socket TCP Créée et configurée sur le port %d\n", TCP_PORT);

    // Créer un processus fils pour gérer les transferts de fichiers
    pid_t pid = fork();
    if (pid == 0) {
        // Processus fils : gère les transferts de fichiers
        close(dS_udp);  // Le fils n'a pas besoin de la socket UDP
        handle_file_transfers(dS_tcp);
        exit(0);
    } else if (pid < 0) {
        perror("Erreur fork");
        close(dS_udp);
        close(dS_tcp);
        exit(EXIT_FAILURE);
    }
    // Le processus parent continue pour gérer l'UDP

    // Configuration de l'adresse locale pour UDP
    struct sockaddr_in aL;
    memset(&aL, 0, sizeof(aL));
    aL.sin_family = AF_INET;
    aL.sin_addr.s_addr = INADDR_ANY;
    aL.sin_port = htons(serverPort);

    // Nommage de la socket UDP uniquement (TCP est déjà bind dans setup_tcp_socket)
    if (bind(dS_udp, (struct sockaddr*) &aL, sizeof(aL)) < 0) {
        perror("Erreur bind socket UDP");
        close(dS_udp);
        close(dS_tcp);
        exit(EXIT_FAILURE);
    }

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
        int recvLen = recvfrom(dS_udp, buffer, BUFFER_SIZE-1, 0, (struct sockaddr*) &aE, &lgA);
        
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
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
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
                        
                        printf("Nouvel utilisateur enregistré: %s @ %s:%d (index: %d)\n", username, client_ip, ntohs(aE.sin_port), client_count);
                        
                        // Envoyer confirmation
                        char response[BUFFER_SIZE];
                        sprintf(response, "Bienvenue %s! Vous êtes connecté.", username);
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                        
                        client_count++;
                    } else {
                        // Limite d'utilisateurs atteinte
                        dict_remove(users_dict, username);  // Annuler l'insertion dans le dictionnaire
                        
                        char response[BUFFER_SIZE] = "Erreur: Limite d'utilisateurs atteinte.";
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
                    }
                }
            } else {
                // Username vide
                char response[BUFFER_SIZE] = "Erreur: Veuillez fournir un nom d'utilisateur valide.";
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*) &aE, lgA);
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
                                if (sendto(dS_udp, forward_msg, strlen(forward_msg), 0, (struct sockaddr*) &dest_addr, sizeof(dest_addr)) < 0) {
                                    perror("Erreur envoi message");
                                    
                                    // Informer l'expéditeur de l'échec
                                    char error_msg[BUFFER_SIZE];
                                    sprintf(error_msg, "Erreur d'envoi du message à %s.", dest_username);
                                    sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                                } else {
                                    char dest_ip[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &(dest_addr.sin_addr), dest_ip, INET_ADDRSTRLEN);
                                    printf("Message redirigé à %s (%s:%d)\n", dest_username, dest_ip, ntohs(dest_addr.sin_port));
                                    
                                    // Confirmer l'envoi à l'expéditeur
                                    char confirm_msg[BUFFER_SIZE];
                                    sprintf(confirm_msg, "Message envoyé à %s.", dest_username);
                                    sendto(dS_udp, confirm_msg, strlen(confirm_msg), 0, (struct sockaddr*) &aE, lgA);
                                }
                            } else {
                                // Destinataire inactif ou index invalide
                                char error_msg[BUFFER_SIZE];
                                sprintf(error_msg, "Erreur: Utilisateur '%s' n'est plus connecté.", dest_username);
                                sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                            }
                        } else {
                            // Destinataire introuvable
                            char error_msg[BUFFER_SIZE];
                            sprintf(error_msg, "Erreur: Utilisateur '%s' introuvable.", dest_username);
                            sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                        }
                    } else {
                        // Nom de destinataire invalide
                        char error_msg[BUFFER_SIZE] = "Erreur: Format de destinataire invalide.";
                        sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                    }
                } else {
                    // Pas d'espace après le destinataire
                    char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                    sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
                }
            } else {
                // Format de message invalide, pas d'esperluette
                char error_msg[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@message &destinataire message'.";
                sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
            }
        } 

        // Traitement de la commande d'upload de fichier
        else if (strncmp(buffer, UPLOAD_CMD, strlen(UPLOAD_CMD)) == 0) {
            // Format attendu: "@upload filename"
            char *filename = buffer + strlen(UPLOAD_CMD) + 1;
            while (*filename == ' ') filename++; // Ignorer les espaces

            if (strlen(filename) > 0) {
                // Chercher l'utilisateur qui fait la demande
                char sender_username[BUFFER_SIZE] = "inconnu";
                for (int i = 0; i < client_count; i++) {
                    if (clients[i].active && 
                        clients[i].addr.sin_addr.s_addr == aE.sin_addr.s_addr && 
                        clients[i].addr.sin_port == aE.sin_port) {
                        strncpy(sender_username, clients[i].username, BUFFER_SIZE - 1);
                        break;
                    }
                }

                // Envoyer le port TCP au client via la socket UDP
                char upload_response[BUFFER_SIZE];
                sprintf(upload_response, "UPLOAD_PORT %d", TCP_PORT);
                sendto(dS_udp, upload_response, strlen(upload_response), 0, (struct sockaddr*) &aE, lgA);
                
                printf("Notification d'upload envoyée à %s pour le fichier %s\n", sender_username, filename);
            } else {
                char error_msg[BUFFER_SIZE] = "Erreur: Veuillez spécifier un nom de fichier.";
                sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*) &aE, lgA);
            }
        } else {
            // Message standard, format non reconnu
            printf("Message standard reçu\n");
            
            // Informer l'expéditeur que le format du message n'est pas reconnu
            char help_msg[BUFFER_SIZE] = "Format non reconnu. Utilisez '@login username' pour vous connecter, '@message &destinataire message' pour envoyer un message ou '@upload filename' pour envoyer un fichier.";
            sendto(dS_udp, help_msg, strlen(help_msg), 0, (struct sockaddr*) &aE, lgA);
        }
    }
    
    // Libération des ressources
    dict_free(users_dict);
    free(clients);
    close(dS_udp);
    close(dS_tcp);
    
    return 0;
}