#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "globalVariables.h"
#include "dict.h"

#define BUFFER_SIZE 1000
#define LOGIN_CMD "login"

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
                    // On pourrait modifier dict_insert pour permettre la mise à jour
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
        } else {
            // Autre type de message, on pourrait vérifier si l'IP est associée à un utilisateur
            // et traiter en conséquence
            printf("Message standard reçu\n");
        }
    }
    
    // Libération des ressources
    dict_free(users);
    close(dS);
    
    return 0;
}
