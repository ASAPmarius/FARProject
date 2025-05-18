#ifndef SIMPLE_DICT_H
#define SIMPLE_DICT_H

#include <stddef.h>

/**
 * Représente une paire (username → password)
 */
typedef struct {
    char *key;    /* username */
    char *value;  /* password */
} Entry;

/**
 * Dictionnaire simple : tableau dynamique d'Entry
 */
typedef struct {
    Entry *entries;   /* tableau d'entrées */
    size_t count;     /* nb d'entrées occupées */
    size_t capacity;  /* taille du tableau alloué */
} SimpleDict;

/* Crée et initialise un dictionnaire */
SimpleDict *dict_create(void);

/* Libère un dictionnaire et ses ressources */
void dict_free(SimpleDict *d);

/* Insère une nouvelle clé/valeur, redimensionne si seuil atteint */
int dict_insert(SimpleDict *d, const char *key, const char *value);

/* Récupère la valeur associée à la clé, NULL si absent */
const char *dict_get(SimpleDict *d, const char *key);

/* Supprime une clé et sa valeur, renvoie 1 si supprimé, 0 sinon */
int dict_remove(SimpleDict *d, const char *key);

#endif