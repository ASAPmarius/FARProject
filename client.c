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
#include "globalVariables.h"
#include "dict.h"

#define BUF_SIZE 1000
#define BUFFER_SIZE 1000
#define LOGIN_CMD "@login"
#define MESSAGE_CMD "@message"
#define UPLOAD_CMD "@upload"
#define DOWNLOAD_CMD "@download"
#define FILE_BUFFER_SIZE 4096
#define TCP_PORT 8888

typedef struct {
    int sockfd;
    struct sockaddr_in servaddr;
    socklen_t len;
} structThread;

// Fonction simplifiée pour envoyer un fichier
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

// Fonction simplifiée pour télécharger un fichier
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

    // Réception de la réponse initiale
    char buffer[FILE_BUFFER_SIZE];
    ssize_t bytes_received = recv(sock, buffer, FILE_BUFFER_SIZE, 0);
    if (bytes_received <= 0) {
        printf("Erreur: Pas de réponse du serveur\n");
        fclose(file);
        close(sock);
        return -1;
    }

    if (strncmp(buffer, "FILE_NOT_FOUND", 13) == 0) {
        printf("Erreur: Fichier non trouvé sur le serveur\n");
        fclose(file);
        close(sock);
        remove(local_path);  // Supprimer le fichier vide
        return -1;
    }

    // Écriture des données reçues dans le fichier
    fwrite(buffer, 1, bytes_received, file);

    // Continuer à recevoir le reste du fichier
    long total_received = bytes_received;
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

// Thread pour envoyer les messages
void *sendThread(void *arg) {
    structThread *t = (structThread *)arg;
    char msg[BUF_SIZE];
    
    while (fgets(msg, BUF_SIZE, stdin) != NULL) {
        size_t len = strlen(msg);
        if (len > 0 && msg[len-1] == '\n') msg[len-1] = '\0';
        
        // Gestion de la commande upload
        if (strncmp(msg, UPLOAD_CMD, strlen(UPLOAD_CMD)) == 0) {
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
        // Vérification du format pour les messages
        else if (strncmp(msg, MESSAGE_CMD, strlen(MESSAGE_CMD)) == 0) {
            char *message_start = msg + strlen(MESSAGE_CMD) + 1;
            if (strlen(message_start) > 0 && message_start[0] != '&') {
                printf("Format incorrect. Utilisez: '@message &destinataire message'\n");
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
    while (1) {
        ssize_t n = recvfrom(t->sockfd, buffer, BUF_SIZE-1, 0,
                             (struct sockaddr *)&t->servaddr, &t->len);
        if (n < 0) {
            perror("recvfrom");
            break;
        }
        buffer[n] = '\0';
        printf("%s\n", buffer);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    //on lance le client avec l'adresse IP du serveur en argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]); // Adresse IP du serveur 
        exit(EXIT_FAILURE);
    }

    // Demande du nom d'utilisateur
    printf("Entrez nom d'utilisateur: ");
    char username[50]; // max 50 caractères pour le nom d'utilisateur
    if (!fgets(username, sizeof(username), stdin)) {
        perror("fgets"); // Erreur de lecture du nom d'utilisateur
        exit(EXIT_FAILURE);
    }
    username[strcspn(username, "\n")] = '\0';

    // Création de la socket UDP
    int dS = socket(PF_INET, SOCK_DGRAM, 0);
    if (dS < 0) {
        perror("socket"); // Erreur de création de la socket
        exit(EXIT_FAILURE);
    }

    // Configuration de l'adresse du serveur
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) { // Adresse IP du serveur dans argv[1] mais on peut changer
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }
    socklen_t servlen = sizeof(servaddr);

    // Envoi de la commande de login
    char login_msg[BUF_SIZE]; // Message de login
    snprintf(login_msg, BUF_SIZE, "%s %s", LOGIN_CMD, username); // Format: "@login username"
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

    //argument pour les threads
    structThread arg = {
        .sockfd = dS,
        .servaddr = servaddr,
        .len = servlen
    };

    // Lancement des threads d'envoi et de réception
    pthread_t tid_send, tid_recv;
    if (pthread_create(&tid_send, NULL, sendThread, &arg) != 0) { //création du thread d'envoi du message
        perror("pthread_create sendThread");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&tid_recv, NULL, recvThread, &arg) != 0) { //création du thread de réception du message
        perror("pthread_create recvThread");
        exit(EXIT_FAILURE);
    }

    // Attente de la fin du thread d'envoi puis arrêt du thread de réception
    pthread_join(tid_send, NULL);
    pthread_cancel(tid_recv);
    close(dS);

    return 0;
}