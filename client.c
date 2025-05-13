#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "globalVariables.h"
#include "dict.h"
#include "chatroom.h"

#define BUF_SIZE 1000

typedef struct {
    int sockfd;
    struct sockaddr_in servaddr;
    socklen_t len;
} thread_arg_t;

int running = 1; // Flag pour contrôler l'exécution des threads

// Gestionnaire de signal pour fermeture propre
void handle_signal(int sig) {
    printf("\nFermeture du client (signal %d)...\n", sig);
    running = 0;
}

// Thread pour envoyer les messages saisis au clavier
void *sendThread(void *arg) {
    thread_arg_t *t = (thread_arg_t *)arg;
    char msg[BUF_SIZE]; // Buffer pour le message(taille max 1000 caractères)
    
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
        if (len > 0 && msg[len-1] == '\n') msg[len-1] = '\0'; // Enlève le '\n' à la fin ajouté automatiquement par fgets
        
        // Format qui vérifie et guide l'utilisateur pour formater correctement leurs messages
        if (strncmp(msg, MESSAGE_CMD, strlen(MESSAGE_CMD)) == 0) {
            // Vérifier si le format est correct: @message &destinataire message
            char *message_start = msg + strlen(MESSAGE_CMD) + 1; // +1 pour l'espace
            if (strlen(message_start) > 0 && message_start[0] != '&') {
                printf("Format incorrect. Utilisez: '@message &destinataire message'\n");
                continue; // Ne pas envoyer le message mal formaté
            }
        } else if (strncmp(msg, ROOMSG_CMD, strlen(ROOMSG_CMD)) == 0) {
            // Vérifier si le format est correct: @roomsg nom_salle message
            char *params = msg + strlen(ROOMSG_CMD) + 1; // +1 pour l'espace
            char *space = strchr(params, ' ');
            if (!space || space == params) {
                printf("Format incorrect. Utilisez: '@roomsg nom_salle message'\n");
                continue; // Ne pas envoyer le message mal formaté
            }
        }
        
        if (sendto(t->sockfd, msg, strlen(msg), 0,
                   (struct sockaddr *)&t->servaddr, t->len) < 0) {
            perror("sendto");
            break;
        }
    }
    return NULL;
}

// Thread pour recevoir et afficher les messages du serveur
void *recvThread(void *arg) {
    thread_arg_t *t = (thread_arg_t *)arg;
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
            // Trouver la fin du nom de la salle
            char *end_bracket = strchr(buffer, ']');
            if (end_bracket) {
                // Mettre en évidence le message de salon
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

    // On lance le client avec l'adresse IP du serveur en argument
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
    printf("Pour envoyer un message privé: %s &destinataire votre_message\n", MESSAGE_CMD);
    printf("===================\n\n");

    //argument pour les threads
    thread_arg_t arg = {
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