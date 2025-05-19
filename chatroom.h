#ifndef CHATROOM_H
#define CHATROOM_H

#include <stddef.h>

/**
 * Représente une salle de discussion (chat room)
 */
typedef struct {
    char name[50];               /* Nom de la salle */
    int max_members;             /* Nombre maximum de membres autorisés */
    int *member_indices;         /* Tableau d'indices vers les clients */
    int member_count;            /* Nombre actuel de membres */
    int active;                  /* Indique si la salle est active */
} ChatRoom;

/* Crée et initialise une nouvelle salle de discussion */
ChatRoom *chatroom_create(const char *name, int max_members);

/* Libère une salle de discussion et ses ressources */
void chatroom_free(ChatRoom *room);

/* Ajoute un client à une salle de discussion */
int chatroom_add_member(ChatRoom *room, int client_index);

/* Supprime un client d'une salle de discussion */
int chatroom_remove_member(ChatRoom *room, int client_index);

/* Vérifie si un client est membre d'une salle */
int chatroom_is_member(ChatRoom *room, int client_index);

/* Retourne le nombre de membres dans une salle */
int chatroom_get_member_count(ChatRoom *room);

/* Retourne la capacité maximale d'une salle */
int chatroom_get_max_members(ChatRoom *room);

/* Vérifie si une salle est pleine */
int chatroom_is_full(ChatRoom *room);

/* Retourne le nom d'une salle */
const char *chatroom_get_name(ChatRoom *room);

#endif