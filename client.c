#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include "globalVariables.h"
#include "dict.h"
#include "chatroom.h"

#define BUF_SIZE 1000
#define BUFFER_SIZE 1000
#define LOGIN_CMD "@login"
#define MESSAGE_CMD "@message"
#define UPLOAD_CMD "@upload"
#define DOWNLOAD_CMD "@download"
#define FILE_BUFFER_SIZE 4096
pthread_t tid_send, tid_recv;
int running = 1; // Flag pour contrôler l'exécution des threads

typedef struct {
    int sockfd;
    struct sockaddr_in servaddr;
    socklen_t len;
} structThread;

// Fonction pour envoyer un fichier
int send_file(const char* filename) {
    // Ouvrir le fichier en lecture
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Erreur: Impossible d'ouvrir le fichier %s\n", filename);
        return -1;
    }

    // Créer une socket TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Erreur: Création socket TCP\n");
        fclose(file);
        return -1;
    }

    // Obtenir la taille du fichier
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Configuration de l'adresse du serveur
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // À modifier avec l'IP du serveur

    // Connexion au serveur
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Erreur: Connexion au serveur\n");
        close(sock);
        fclose(file);
        return -1;
    }

    // Envoyer la commande @upload pour indiquer qu'il s'agit d'un upload
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "%s %s", UPLOAD_CMD, filename);
    if (send(sock, command, strlen(command), 0) < 0) {
        printf("Erreur: Envoi de la commande upload\n");
        fclose(file);
        close(sock);
        return -1;
    }

    // Petite pause pour s'assurer que la commande est bien reçue
    usleep(100000);  // 100ms

    // Envoi du contenu du fichier
    char buffer[FILE_BUFFER_SIZE];
    size_t bytes_read;
    long total_sent = 0;

    while ((bytes_read = fread(buffer, 1, FILE_BUFFER_SIZE, file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            printf("Erreur: Envoi du fichier\n");
            break;
        }
        total_sent += bytes_read;
        printf("Progression: %ld/%ld octets envoyés\r", total_sent, file_size);
        fflush(stdout);
    }

    printf("Fichier %s envoyé avec succès\n", filename);
    fclose(file);
    close(sock);
    return 0;
}

// Fonction pour télécharger un fichier
int download_file(const char* filename) {
    // Créer une socket TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Erreur: Création socket TCP\n");
        return -1;
    }

    // Configuration de l'adresse du serveur
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // À modifier avec l'IP du serveur

    // Connexion au serveur
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Erreur: Connexion au serveur\n");
        close(sock);
        return -1;
    }

    // Envoyer la commande de téléchargement avec le nom du fichier
    char command[BUFFER_SIZE];
    snprintf(command, BUFFER_SIZE, "%s %s", DOWNLOAD_CMD, filename);
    if (send(sock, command, strlen(command), 0) < 0) {
        printf("Erreur: Envoi de la commande de téléchargement\n");
        close(sock);
        return -1;
    }

    // Création du fichier local
    char local_path[256];
    snprintf(local_path, sizeof(local_path), "downloads/%s", filename);
    FILE* file = fopen(local_path, "wb");
    if (!file) {
        printf("Erreur: Impossible de créer le fichier %s\n", local_path);
        close(sock);
        return -1;
    }

    // D'abord recevoir le message de contrôle
    char control_msg[BUFFER_SIZE];
    ssize_t control_size = recv(sock, control_msg, BUFFER_SIZE - 1, 0);
    if (control_size <= 0) {
        printf("Erreur: Pas de réponse du serveur\n");
        fclose(file);
        close(sock);
        return -1;
    }
    control_msg[control_size] = '\0';

    // Vérifier le message de contrôle
    if (strcmp(control_msg, "FILE_NOT_FOUND") == 0) {
        printf("Erreur: Fichier non trouvé sur le serveur\n");
        fclose(file);
        close(sock);
        remove(local_path);
        return -1;
    }
    else if (strcmp(control_msg, "FILE_SEND_START") != 0) {
        printf("Erreur: Réponse inattendue du serveur\n");
        fclose(file);
        close(sock);
        remove(local_path);
        return -1;
    }

    // Recevoir le contenu du fichier
    char buffer[FILE_BUFFER_SIZE];
    ssize_t bytes_received;
    long total_received = 0;

    // Attendre un court instant pour que le serveur commence à envoyer le fichier
    usleep(100000);  // 100ms

    while ((bytes_received = recv(sock, buffer, FILE_BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        total_received += bytes_received;
        printf("Téléchargement en cours: %ld octets reçus\r", total_received);
        fflush(stdout);
    }

    printf("\nFichier %s téléchargé avec succès (%ld octets)\n", filename, total_received);
    fclose(file);
    close(sock);
    return 0;
}

// Gestionnaire de signal pour fermeture propre
void handle_signal(int sig) {
    printf("\nFermeture du client (signal %d)...\n", sig);
    running = 0;
    
    // Cancel both threads to force immediate exit
    pthread_cancel(tid_send);
    pthread_cancel(tid_recv);
    
    exit(EXIT_SUCCESS);
}

// Thread pour envoyer les messages saisis au clavier
void *sendThread(void *arg) {
    thread_arg_t *t = (thread_arg_t *)arg;
    char msg[BUF_SIZE];
    
    // Afficher les instructions d'utilisation pour les salons de chat
    printf("\n=== Commandes pour les salons de chat ===\n");
    printf("%s nom_salle max_membres - Créer une nouvelle salle\n", CREATEROOM_CMD);
    printf("%s nom_salle - Rejoindre une salle existante\n", JOINROOM_CMD);
    printf("%s nom_salle - Quitter une salle\n", LEAVEROOM_CMD);
    printf("%s - Lister toutes les salles disponibles\n", LISTROOMS_CMD);
    printf("%s nom_salle - Lister les membres d'une salle\n", LISTMEMBERS_CMD);
    printf("%s nom_salle message - Envoyer un message à tous les membres d'une salle\n", ROOMSG_CMD);
    printf("===================================\n\n");
    
    while (running && fgets(msg, BUF_SIZE, stdin) != NULL) {
        size_t len = strlen(msg);
        if (len > 0 && msg[len-1] == '\n') msg[len-1] = '\0';
        
        if (strncmp(msg, MESSAGE_CMD, strlen(MESSAGE_CMD)) == 0) {
            char *message_start = msg + strlen(MESSAGE_CMD) + 1;
            if (strlen(message_start) > 0 && message_start[0] != '&') {
                printf("Format incorrect. Utilisez: '%s &destinataire message'\n", MESSAGE_CMD);
                continue;
            }
        } else if (strncmp(msg, ROOMSG_CMD, strlen(ROOMSG_CMD)) == 0) {
            char *params = msg + strlen(ROOMSG_CMD) + 1;
            char *space = strchr(params, ' ');
            if (!space || space == params) {
                printf("Format incorrect. Utilisez: '%s nom_salle message'\n", ROOMSG_CMD);
                continue;
            }
        }
        // Gestion de la commande upload
        else if (strncmp(msg, UPLOAD_CMD, strlen(UPLOAD_CMD)) == 0) {
            char *filename = msg + strlen(UPLOAD_CMD) + 1;
            while (*filename == ' ') filename++; // Ignore les espaces
            if (strlen(filename) > 0) {
                send_file(filename);
                continue;
            }
        }
        // Gestion de la commande download
        else if (strncmp(msg, DOWNLOAD_CMD, strlen(DOWNLOAD_CMD)) == 0) {
            char *filename = msg + strlen(DOWNLOAD_CMD) + 1;
            while (*filename == ' ') filename++; // Ignore les espaces
            if (strlen(filename) > 0) {
                download_file(filename);
                continue;
            }
        }
        
        if (sendto(t->sockfd, msg, strlen(msg), 0, (struct sockaddr *)&t->servaddr, t->len) < 0) {
            perror("sendto");
            break;
        }
    }
    return NULL;
}

// Thread pour recevoir et afficher les messages du serveur
void *recvThread(void *arg) {
    structThread *t = (structThread *)arg;
    char buffer[BUF_SIZE];
    while (running) {
        ssize_t n = recvfrom(t->sockfd, buffer, BUF_SIZE-1, 0,
                             (struct sockaddr *)&t->servaddr, &t->len);
        if (n < 0) {
            perror("recvfrom");
            break;
        }
        buffer[n] = '\0';
        
        // Affichage spécial pour les messages de salle (qui commencent par "[nom_salle]")
        if (buffer[0] == '[') {
            char *end_bracket = strchr(buffer, ']');
            if (end_bracket) {
                printf("\033[1;34m%s\033[0m\n", buffer); // Bleu gras
            } else {
                printf("%s\n", buffer);
            }
        } else {
            printf("%s\n", buffer);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    // Configuration du gestionnaire de signaux
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Demande du nom d'utilisateur
    printf("Entrez nom d'utilisateur: ");
    char username[50];
    if (!fgets(username, sizeof(username), stdin)) {
        perror("fgets");
        exit(EXIT_FAILURE);
    }
    username[strcspn(username, "\n")] = '\0';

    // Demande du mot de passe
    printf("Entrez mot de passe: ");
    char password[50];
    if (!fgets(password, sizeof(password), stdin)) {
        perror("fgets");
        exit(EXIT_FAILURE);
    }
    password[strcspn(password, "\n")] = '\0';

    // Création de la socket UDP
    int dS = socket(PF_INET, SOCK_DGRAM, 0);
    if (dS < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse du serveur
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }
    socklen_t servlen = sizeof(servaddr);

    // Envoi de la commande de login avec username et password
    char login_msg[BUF_SIZE];
    snprintf(login_msg, BUF_SIZE, "%s %s %s", LOGIN_CMD, username, password);
    if (sendto(dS, login_msg, strlen(login_msg), 0,
               (struct sockaddr *)&servaddr, servlen) < 0) {
        perror("sendto login");
        exit(EXIT_FAILURE);
    }

    // Afficher les instructions pour l'utilisateur
    printf("\n=== Instructions ===\n");
    printf("Pour envoyer un message: @message &destinataire votre_message\n");
    printf("Pour envoyer un fichier: @upload nom_fichier.extension\n");
    printf("Pour télécharger un fichier: @download nom_fichier.extension\n");
    printf("===================\n\n");

    // Créer le répertoire downloads s'il n'existe pas
    mkdir("downloads", 0777);

    // Argument pour les threads
    thread_arg_t arg = {
        .sockfd = dS,
        .servaddr = servaddr,
        .len = servlen
    };

    // Lancement des threads d'envoi et de réception
    if (pthread_create(&tid_send, NULL, sendThread, &arg) != 0) {
        perror("pthread_create sendThread");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&tid_recv, NULL, recvThread, &arg) != 0) {
        perror("pthread_create recvThread");
        exit(EXIT_FAILURE);
    }

    // Attente de la fin du thread d'envoi puis arrêt du thread de réception
    pthread_join(tid_send, NULL);
    pthread_cancel(tid_recv);
    close(dS);

    return 0;
}
