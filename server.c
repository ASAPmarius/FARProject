#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include "globalVariables.h"
#include "dict.h"
#include "chatroom.h"

#define BUFFER_SIZE 2000
#define FILE_BUFFER_SIZE 4096
#define LOGIN_CMD "@login"
#define MESSAGE_CMD "@message"
#define UPLOAD_CMD "@upload"
#define DOWNLOAD_CMD "@download"
#define HELP_CMD "@help"
#define CREDITS_CMD "@credits"
#define MAX_USERS 100

// Flag pour contrôler la boucle principale
static volatile sig_atomic_t running = 1;

// Structure pour stocker les informations complètes d'un client
typedef struct {
    char username[50];               // Nom d'utilisateur
    struct sockaddr_in addr;         // Adresse du client
    int active;                      // Flag si actif
    int joined_rooms[MAX_ROOMS];     // Salles auxquelles il a adhéré
    int room_count;                  // Nombre de salles
} ClientInfo;

// Variables globales pour les sockets
int dS_udp;  // Socket UDP pour la messagerie
int dS_tcp;  // Socket TCP pour les fichiers

// Variables globales pour le système de chat
SimpleDict *users_dict;              // Dictionnaire username → password
ClientInfo *clients;                 // Tableau des clients
int client_count = 0;                // Nombre de clients
ChatRoom *rooms[MAX_ROOMS];          // Tableau des salles
int room_count = 0;                  // Nombre de salles
SimpleDict *room_dict;               // Dictionnaire nom_salle → index

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
    // Nommage de la socket
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
        mkdir("uploads", 0777);
        printf("Dossier uploads créé\n");
    }

    // Créer le chemin complet du fichier
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "uploads/%s", filename);
    printf("Réception du fichier: %s\n", filepath);

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
    // Enlever les espaces au début du nom de fichier
    while (*filename == ' ') filename++;
    
    // Vérifier que le nom de fichier est valide
    if (strlen(filename) == 0) {
        printf("Erreur: Nom de fichier vide\n");
        char error_msg[] = "FILE_NOT_FOUND";
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

    // Envoyer un message de succès avant le contenu du fichier
    char success_msg[] = "FILE_SEND_START";
    send(client_socket, success_msg, strlen(success_msg), 0);

    // Petit délai pour s'assurer que le client est prêt
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
    while (running) {
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
                handle_file_download(client_socket, filename);
            }
        }

        close(client_socket);
    }
}

// Diffuse un message à tous les membres d'une salle (sauf expéditeur)
void broadcast_to_room(int room_index, const char *message, const char *sender_username, struct sockaddr_in *sender_addr) {
    if (room_index < 0 || room_index >= room_count || !rooms[room_index]) return;
    ChatRoom *room = rooms[room_index];
    char forward_msg[BUFFER_SIZE];
    sprintf(forward_msg, "[%s] %s: %s", room->name, sender_username, message);

    for (int i = 0; i < room->member_count; i++) {
        int member = room->member_indices[i];
        if (clients[member].active &&
            (sender_addr == NULL ||
             clients[member].addr.sin_addr.s_addr != sender_addr->sin_addr.s_addr ||
             clients[member].addr.sin_port        != sender_addr->sin_port)) {
            if (sendto(dS_udp, forward_msg, strlen(forward_msg), 0,
                       (struct sockaddr*)&clients[member].addr,
                       sizeof(clients[member].addr)) < 0) {
                perror("Erreur envoi message à un membre");
            }
        }
    }
}

// Retourne l'index d'un client à partir de son adresse, ou -1 sinon
int find_client_index(struct sockaddr_in *addr) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active &&
            clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i].addr.sin_port        == addr->sin_port) {
            return i;
        }
    }
    return -1;
}

// Trouve l'index d'une salle par son nom, ou -1 si absente
int find_room_by_name(const char *room_name) {
    const char *idx = dict_get(room_dict, room_name);
    return idx ? atoi(idx) : -1;
}

/**
 * Recherche l'index du client via son username (chargé en mémoire).
 * Retourne -1 si introuvable.
 */
static int find_user_index_by_name(const char *username) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Sauvegarde les salles et leurs membres.
 * Format de chaque ligne :
 *   room_id:room_name:max_members:user1,user2,...
 */
void save_rooms_to_file(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror("Error opening rooms file"); return; }

    for (int id = 0; id < room_count; id++) {
        ChatRoom *r = rooms[id];
        if (!r || !r->active) continue;

        // 1) room_id:room_name:max_members:
        fprintf(f, "%d:%s:%d:", id, r->name, r->max_members);

        // 2) liste des membres par username, séparés par des virgules
        for (int j = 0; j < r->member_count; j++) {
            int uid = r->member_indices[j];
            fprintf(f, "%s", clients[uid].username);
            if (j + 1 < r->member_count) fputc(',', f);
        }

        fputc('\n', f);
    }

    fclose(f);
    printf("Rooms saved to %s\n", filename);
}

// Sauvegarde les utilisateurs (index:username:password) dans un fichier
void save_users_to_file(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror("Error opening users file"); return; }
    for (int i = 0; i < client_count; i++) {
        const char *pwd = dict_get(users_dict, clients[i].username);
        if (!pwd) pwd = "";  // Sécurité
        // On écrit id:username:password
        fprintf(f, "%d:%s:%s\n", i, clients[i].username, pwd);
    }
    fclose(f);
    printf("Users saved to %s\n", filename);
}

// Charge les utilisateurs depuis un fichier (index:username:password)
void load_users_from_file(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) { 
        printf("No users file found. Starting fresh.\n"); 
        return; 
    }
    char line[512];
    int max_id = -1;
    while (fgets(line, sizeof(line), f)) {
        int id;
        char username[50], password[50];
        // On attend dorénavant trois champs séparés par ':'
        if (sscanf(line, "%d:%49[^:]:%49s", &id, username, password) == 3 && id < MAX_USERS) {
            if (id > max_id) max_id = id;
            // On stocke username -> password
            dict_insert(users_dict, username, password);
            // On initialise le client en inactif
            strncpy(clients[id].username, username, sizeof(clients[id].username)-1);
            clients[id].active = 0;
            clients[id].room_count = 0;
        }
    }
    client_count = max_id + 1;
    fclose(f);
    printf("Loaded %d users from %s\n", client_count, filename);
}

/**
 * Charge les salles et leurs membres depuis le fichier.
 * S'attend à chaque ligne au format :
 *   room_id:room_name:max_members:user1,user2,...
 */
void load_rooms_from_file(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("No rooms file found. Starting fresh.\n");
        return;
    }

    char line[1024];
    room_count = 0;

    while (fgets(line, sizeof(line), f)) {
        // Découpe des parties
        // On lit d'abord le room_id
        int id, maxm;
        char room_name[MAX_ROOM_NAME_LENGTH];
        char *members_list;

        // On supprime le '\n'
        size_t L = strlen(line);
        if (L && line[L-1] == '\n') line[L-1] = '\0';

        // On parse : id:name:max_members:members...
        char *p1 = strchr(line, ':');
        if (!p1) continue;
        *p1 = '\0';
        id = atoi(line);

        char *p2 = strchr(p1+1, ':');
        if (!p2) continue;
        *p2 = '\0';
        strncpy(room_name, p1+1, MAX_ROOM_NAME_LENGTH-1);

        char *p3 = strchr(p2+1, ':');
        if (!p3) continue;
        *p3 = '\0';
        maxm = atoi(p2+1);

        // La suite, après le troisième ':', est la liste des membres
        members_list = p3+1;

        // Créer la salle à l'index spécifié
        ChatRoom *r = chatroom_create(room_name, maxm);
        if (!r) continue;
        rooms[id] = r;
        if (id >= room_count) room_count = id + 1;

        // Mettre à jour le dictionnaire
        char idx_str[16];
        snprintf(idx_str, sizeof(idx_str), "%d", id);
        dict_insert(room_dict, room_name, idx_str);

        // Parcourir les membres listés
        char *tok = strtok(members_list, ",");
        while (tok) {
            int uid = find_user_index_by_name(tok);
            if (uid >= 0) {
                chatroom_add_member(r, uid);
                clients[uid].joined_rooms[clients[uid].room_count++] = id;
            }
            tok = strtok(NULL, ",");
        }
    }

    fclose(f);
    printf("Loaded %d rooms from %s\n", room_count, filename);
}

// Gestionnaire de signal : sauvegarde et cleanup, puis exit
void handle_signal(int sig) {
    printf("\nFermeture du serveur (signal %d)...\n", sig);
    save_users_to_file("users.txt");
    save_rooms_to_file("rooms.txt");
    for (int i = 0; i < room_count; i++) chatroom_free(rooms[i]);
    dict_free(users_dict);
    dict_free(room_dict);
    free(clients);
    close(dS_udp);
    close(dS_tcp);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    printf("Début programme serveur\n");

    // Configuration du gestionnaire de signaux
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    // Création du dossier uploads s'il n'existe pas
    mkdir("uploads", 0777);
    mkdir("downloads", 0777);

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

    // Configuration de l'adresse locale pour UDP
    struct sockaddr_in aL;
    memset(&aL, 0, sizeof(aL));
    aL.sin_family = AF_INET;
    aL.sin_addr.s_addr = INADDR_ANY;
    aL.sin_port = htons(serverPort);

    // Nommage de la socket UDP uniquement
    if (bind(dS_udp, (struct sockaddr*) &aL, sizeof(aL)) < 0) {
        perror("Erreur nommage socket UDP");
        close(dS_udp);
        close(dS_tcp);
        exit(EXIT_FAILURE);
    }
    printf("Socket UDP bindée sur port %d\n", serverPort);

    // Initialisation des structures
    users_dict = dict_create();
    dict_insert(users_dict, "admin", "admin");
    room_dict = dict_create();
    clients = calloc(MAX_USERS, sizeof(ClientInfo));
    for (int i = 0; i < MAX_USERS; i++) {
        clients[i].active = 0;
        clients[i].room_count = 0;
    }

    // Chargement des utilisateurs et des salles
    load_users_from_file("users.txt");
    load_rooms_from_file("rooms.txt");

    // Créer un processus fils pour gérer les transferts de fichiers
    pid_t pid = fork();
    if (pid == 0) {
        // Processus fils : gère les transferts de fichiers
        close(dS_udp);  // Le fils n'a pas besoin de la socket UDP
        handle_file_transfers(dS_tcp);
        exit(0);
    }
    else if (pid < 0) {
        perror("Erreur fork");
        close(dS_udp);
        close(dS_tcp);
        exit(EXIT_FAILURE);
    }
    
    printf("Serveur prêt, en attente de messages...\n");

    // Boucle principale
    char buffer[BUFFER_SIZE];
    struct sockaddr_in aE;
    socklen_t lgA = sizeof(aE);

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(dS_udp, buffer, BUFFER_SIZE-1, 0, (struct sockaddr*)&aE, &lgA);
        if (n < 0) { perror("recvfrom"); continue; }
        buffer[n] = '\0';

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &aE.sin_addr, client_ip, sizeof(client_ip));
        printf("Reçu de %s:%d : %s\n", client_ip, ntohs(aE.sin_port), buffer);

        int idx = find_client_index(&aE);
        bool is_logged = (idx >= 0 && clients[idx].active);

        // Traitement de la commande @login
        if (strncmp(buffer, LOGIN_CMD, strlen(LOGIN_CMD)) == 0) {
            // Extraction du nom d'utilisateur et du mot de passe
            char *user = strtok(buffer + strlen(LOGIN_CMD) + 1, " ");
            char *pass = strtok(NULL, " ");
            if (!user || !pass) {
                const char *err = "Erreur: Veuillez fournir nom d'utilisateur et mot de passe.";
                sendto(dS_udp, err, strlen(err), 0, (struct sockaddr*)&aE, lgA);
                continue;
            }

            // Recherche en mémoire
            const char *stored = dict_get(users_dict, user);
            int uid = find_user_index_by_name(user);

            if (stored) {
                // Utilisateur connu → vérifier le mot de passe
                if (strcmp(stored, pass) != 0) {
                    const char *err  = "Erreur: Mot de passe incorrect.";
                    sendto(dS_udp, err, strlen(err), 0, (struct sockaddr*)&aE, lgA);
                    const char *hint = "Veuillez retaper : @login <username> <password>";
                    sendto(dS_udp, hint, strlen(hint), 0, (struct sockaddr*)&aE, lgA);
                    continue;
                }
                
                // Authentification réussie
                if (uid < 0) {
                    // Chargé depuis users.txt mais pas encore dans clients[]
                    uid = client_count++;
                    strncpy(clients[uid].username, user, sizeof(clients[uid].username)-1);
                }
                clients[uid].addr       = aE;
                clients[uid].active     = 1;
                clients[uid].room_count = 0;

                char resp[BUFFER_SIZE];
                is_logged = true;
                snprintf(resp, sizeof(resp), "Bienvenue %s! Vous êtes connecté.", user);
                sendto(dS_udp, resp, strlen(resp), 0, (struct sockaddr*)&aE, lgA);

            } else {
                // Nouvel utilisateur
                dict_insert(users_dict, user, pass);
                uid = client_count++;
                strncpy(clients[uid].username, user, sizeof(clients[uid].username)-1);
                clients[uid].addr       = aE;
                clients[uid].active     = 1;
                clients[uid].room_count = 0;

                char resp[BUFFER_SIZE];
                is_logged = true;
                snprintf(resp, sizeof(resp), "Bienvenue %s! Enregistré et connecté.", user);
                sendto(dS_udp, resp, strlen(resp), 0, (struct sockaddr*)&aE, lgA);
            }
            continue;
        }

        if (!is_logged) {
            const char *err =
                "Erreur: vous devez d'abord vous connecter avec\n"
                "@login <username> <password>";
            sendto(dS_udp, err, strlen(err), 0, (struct sockaddr*)&aE, lgA);
            continue;
        }

        // -- PING --
        if (strncmp(buffer, "@ping", 5) == 0) {
            const char *pong = "pong\n";
            sendto(dS_udp, pong, strlen(pong), 0, (struct sockaddr*)&aE, lgA);
        }
        // -- SHUTDOWN --
        else if (strncmp(buffer, "@shutdown", 9) == 0) {
            // Vérifier si l'utilisateur est admin
            int idx = find_client_index(&aE);
            if (idx >= 0 && strcmp(clients[idx].username, "admin") == 0) {
                const char *msg = "Serveur éteint!\n";
                sendto(dS_udp, msg, strlen(msg), 0, (struct sockaddr*)&aE, lgA);
                // On déclenche proprement la fermeture
                raise(SIGINT);
            } else {
                const char *err = "Erreur: accès refusé. Cette commande est réservée à l'utilisateur 'admin'.";
                sendto(dS_udp, err, strlen(err), 0, (struct sockaddr*)&aE, lgA);
            }
        }
        // -- MESSAGE PRIVÉ --
        else if (strncmp(buffer, MESSAGE_CMD, strlen(MESSAGE_CMD)) == 0) {
            char dest[BUFFER_SIZE] = {0}, content[BUFFER_SIZE] = {0};
            char *start = buffer + strlen(MESSAGE_CMD) + 1;
            char *amp = strchr(start, '&');
            if (amp) {
                char *sp = strchr(amp + 1, ' ');
                if (sp) {
                    // Récupérer le nom du destinataire et le contenu
                    size_t dlen = sp - (amp + 1);
                    strncpy(dest, amp + 1, dlen);
                    dest[dlen] = '\0';
                    strncpy(content, sp + 1, BUFFER_SIZE - 1);

                    // Trouver l'index du destinataire
                    int didx = find_user_index_by_name(dest);
                    if (didx >= 0 && clients[didx].active) {
                        // Identifier l'expéditeur
                        int sender_idx = find_client_index(&aE);
                        char sender[50] = "inconnu";
                        if (sender_idx >= 0) {
                            strncpy(sender, clients[sender_idx].username, sizeof(sender)-1);
                        }

                        // Construire et envoyer
                        char forward[BUFFER_SIZE];
                        snprintf(forward, sizeof(forward), "Message de %s: %s", sender, content);
                        if (sendto(dS_udp, forward, strlen(forward), 0,
                                (struct sockaddr*)&clients[didx].addr,
                                sizeof(clients[didx].addr)) < 0) {
                            perror("sendto");
                        } else {
                            char conf[BUFFER_SIZE];
                            snprintf(conf, sizeof(conf), "Message envoyé à %s.", dest);
                            sendto(dS_udp, conf, strlen(conf), 0, (struct sockaddr*)&aE, lgA);
                        }
                    } else {
                        // Destinataire introuvable ou déconnecté
                        char err[BUFFER_SIZE];
                        if (didx < 0) {
                            snprintf(err, sizeof(err),
                                    "Erreur: Utilisateur '%s' introuvable.", dest);
                        } else {
                            snprintf(err, sizeof(err),
                                    "Erreur: Utilisateur '%s' non connecté.", dest);
                        }
                        sendto(dS_udp, err, strlen(err), 0, (struct sockaddr*)&aE, lgA);
                    }
                } else {
                    const char *e = "Format invalide. Utilisez '@message &destinataire message'.";
                    sendto(dS_udp, e, strlen(e), 0, (struct sockaddr*)&aE, lgA);
                }
            } else {
                const char *e = "Format invalide. Utilisez '@message &destinataire message'.";
                sendto(dS_udp, e, strlen(e), 0, (struct sockaddr*)&aE, lgA);
            }
        }
        // Traitement de la commande d'upload de fichier
        else if (strncmp(buffer, UPLOAD_CMD, strlen(UPLOAD_CMD)) == 0) {
            char *filename = buffer + strlen(UPLOAD_CMD) + 1;
            while (*filename == ' ') filename++; // Ignorer les espaces

            if (strlen(filename) > 0) {
                // Chercher l'utilisateur qui fait la demande
                char sender_username[BUFFER_SIZE] = "inconnu";
                int sender_idx = find_client_index(&aE);
                if (sender_idx >= 0) {
                    strncpy(sender_username, clients[sender_idx].username, BUFFER_SIZE - 1);
                }

                // Envoyer le port TCP au client via la socket UDP
                char upload_response[BUFFER_SIZE];
                sprintf(upload_response, "UPLOAD_PORT %d", TCP_PORT);
                sendto(dS_udp, upload_response, strlen(upload_response), 0, (struct sockaddr*)&aE, lgA);
                
                printf("Notification d'upload envoyée à %s pour le fichier %s\n", sender_username, filename);
            } 
            else {
                char error_msg[BUFFER_SIZE] = "Format attendu: '@upload filename'";
                sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*)&aE, lgA);
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
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                    continue;
                }
                
                // Vérifier si la salle existe déjà
                if (dict_get(room_dict, room_name) != NULL) {
                    char response[BUFFER_SIZE];
                    sprintf(response, "Erreur: Une salle nommée '%s' existe déjà.", room_name);
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                } else if (room_count >= MAX_ROOMS) {
                    // Nombre maximum de salles atteint
                    char response[BUFFER_SIZE] = "Erreur: Nombre maximum de salles atteint.";
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                } else {
                    // Créer la nouvelle salle
                    ChatRoom *new_room = chatroom_create(room_name, max_members);
                    if (!new_room) {
                        char response[BUFFER_SIZE] = "Erreur: Impossible de créer la salle.";
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                            sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                        } else {
                            char response[BUFFER_SIZE];
                            sprintf(response, "Salle '%s' créée avec succès.", room_name);
                            sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                        }
                        room_count++;
                    }
                }
            } else {
                // Format invalide
                char response[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@createroom nom_salle max_membres'.";
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
            } else {
                // Trouver le client qui souhaite rejoindre
                int client_index = find_client_index(&aE);
                if (client_index < 0) {
                    char response[BUFFER_SIZE] = "Erreur: Vous n'êtes pas connecté.";
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                } else {
                    // Vérifier si le client est déjà membre
                    if (chatroom_is_member(rooms[room_index], client_index)) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Vous êtes déjà membre de la salle '%s'.", room_name);
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                    } else if (chatroom_is_full(rooms[room_index])) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Erreur: La salle '%s' est pleine.", room_name);
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                    } else {
                        // Ajouter le client à la salle
                        chatroom_add_member(rooms[room_index], client_index);                  
                        // Ajouter la salle à la liste des salles du client
                        if (clients[client_index].room_count < MAX_ROOMS) {
                            clients[client_index].joined_rooms[clients[client_index].room_count++] = room_index;
                            
                            char response[BUFFER_SIZE];
                            sprintf(response, "Vous avez rejoint la salle '%s'.", room_name);
                            sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                            
                            // Notifier les autres membres
                            char notification[BUFFER_SIZE];
                            sprintf(notification, "%s a rejoint la salle.", clients[client_index].username);
                            broadcast_to_room(room_index, notification, "Serveur", &aE);
                        } else {
                            chatroom_remove_member(rooms[room_index], client_index);
                            
                            char response[BUFFER_SIZE] = "Erreur: Vous avez rejoint trop de salles.";
                            sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
            } else {
                // Trouver le client qui souhaite quitter
                int client_index = find_client_index(&aE);
                if (client_index < 0) {
                    char response[BUFFER_SIZE] = "Erreur: Vous n'êtes pas connecté.";
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                } else {
                    // Vérifier si le client est membre
                    if (!chatroom_is_member(rooms[room_index], client_index)) {
                        char response[BUFFER_SIZE];
                        sprintf(response, "Erreur: Vous n'êtes pas membre de la salle '%s'.", room_name);
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                        
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
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
            } else {
                ChatRoom *room = rooms[room_index];
                
                if (chatroom_get_member_count(room) == 0) {
                    char response[BUFFER_SIZE];
                    sprintf(response, "La salle '%s' ne contient aucun membre.", room_name);
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                    
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
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
                        sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                    } else {
                        // Trouver le client qui envoie le message
                        int client_index = find_client_index(&aE);
                        if (client_index < 0) {
                            char response[BUFFER_SIZE] = "Erreur: Vous n'êtes pas connecté.";
                            sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                        } else {
                            // Vérifier si le client est membre de la salle
                            if (!chatroom_is_member(rooms[room_index], client_index)) {
                                char response[BUFFER_SIZE];
                                sprintf(response, "Erreur: Vous n'êtes pas membre de la salle '%s'.", room_name);
                                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                            } else {
                                // Diffuser le message à tous les membres de la salle
                                broadcast_to_room(room_index, message_content, clients[client_index].username, &aE);
                                
                                // Confirmer l'envoi
                                char confirm_msg[BUFFER_SIZE];
                                sprintf(confirm_msg, "Message envoyé à la salle '%s'.", room_name);
                                sendto(dS_udp, confirm_msg, strlen(confirm_msg), 0, (struct sockaddr*)&aE, lgA);
                            }
                        }
                    }
                } else {
                    // Nom de salle invalide
                    char response[BUFFER_SIZE] = "Erreur: Nom de salle invalide.";
                    sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
                }
            } else {
                // Format invalide
                char response[BUFFER_SIZE] = "Erreur: Format invalide. Utilisez '@roomsg nom_salle message'.";
                sendto(dS_udp, response, strlen(response), 0, (struct sockaddr*)&aE, lgA);
            }
        } 
        else if (strncmp(buffer, HELP_CMD, strlen(HELP_CMD)) == 0) {
            FILE *file = fopen("commandes.txt", "r");
            if (file == NULL) {
                char error_msg[] = "Erreur : impossible d'ouvrir le fichier commandes.txt\n";
                sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*)&aE, lgA);
            } 
            else {
                char line[512];
                // Lire et envoyer ligne par ligne
                while (fgets(line, sizeof(line), file)) {
                    sendto(dS_udp, line, strlen(line), 0, (struct sockaddr*)&aE, lgA);
                }
                fclose(file);
            }
        }
        else if (strncmp(buffer, CREDITS_CMD, strlen(CREDITS_CMD)) == 0) {
            FILE *file = fopen("credits.txt", "r");
            if (file == NULL) {
                char error_msg[] = "Erreur : impossible d'ouvrir le fichier credits.txt\n";
                sendto(dS_udp, error_msg, strlen(error_msg), 0, (struct sockaddr*)&aE, lgA);
            } 
            else {
                char line[512];
                // Lire et envoyer ligne par ligne
                while (fgets(line, sizeof(line), file)) {
                    sendto(dS_udp, line, strlen(line), 0, (struct sockaddr*)&aE, lgA);
                }
                fclose(file);
            }
        }
        else {
            // Message standard, format non reconnu
            printf("Message standard reçu\n");
            
            // Informer l'expéditeur que le format du message n'est pas reconnu
            char help_msg[BUFFER_SIZE] = 
                "Format non reconnu. Commandes disponibles:\n"
                "@message &destinataire message - Envoyer un message privé\n"
                "@createroom nom_salle max_membres - Créer une salle\n"
                "@joinroom nom_salle - Rejoindre une salle\n"
                "@leaveroom nom_salle - Quitter une salle\n"
                "@listrooms - Lister les salles disponibles\n"
                "@listmembers nom_salle - Lister les membres d'une salle\n"
                "@roomsg nom_salle message - Envoyer un message à une salle\n"
                "@upload nom_fichier - Envoyer un fichier au serveur\n"
                "@download nom_fichier - Télécharger un fichier du serveur\n"
                "@help - Liste de toutes les commandes\n";
            
            sendto(dS_udp, help_msg, strlen(help_msg), 0, (struct sockaddr*)&aE, lgA);
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
    close(dS_udp);
    close(dS_tcp);
    
    return 0;
}