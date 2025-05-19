#include "dict.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 2
#define LOAD_FACTOR_THRESHOLD 0.8

/* Crée et initialise un dictionnaire */
SimpleDict *dict_create(void) {
    SimpleDict *d = malloc(sizeof(SimpleDict));
    if (!d) return NULL;
    d->entries = malloc(INITIAL_CAPACITY * sizeof(Entry));
    if (!d->entries) {
        free(d);
        return NULL;
    }
    d->count = 0;
    d->capacity = INITIAL_CAPACITY;
    return d;
}

/* Libère un dictionnaire et ses ressources */
void dict_free(SimpleDict *d) {
    if (!d) return;
    for (size_t i = 0; i < d->count; i++) {
        free(d->entries[i].key);
        free(d->entries[i].value);
    }
    free(d->entries);
    free(d);
}

/* Redimensionne le tableau d'entrées */
static int dict_resize(SimpleDict *d) {
    size_t new_capacity = d->capacity * 2;
    Entry *new_entries = realloc(d->entries, new_capacity * sizeof(Entry));
    if (!new_entries) return 0;
    d->entries = new_entries;
    d->capacity = new_capacity;
    return 1;
}

/* Insère une nouvelle clé/valeur, redimensionne si nécessaire */
int dict_insert(SimpleDict *d, const char *key, const char *value) {
    if (!d || !key || !value) return 0;

    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            free(d->entries[i].value);
            d->entries[i].value = strdup(value);
            return 1;
        }
    }

    if (d->count >= d->capacity * LOAD_FACTOR_THRESHOLD) {
        if (!dict_resize(d)) return 0;
    }

    d->entries[d->count].key = strdup(key);
    d->entries[d->count].value = strdup(value);
    d->count++;
    return 1;
}

/* Récupère la valeur associée à la clé */
const char *dict_get(SimpleDict *d, const char *key) {
    if (!d || !key) return NULL;
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            return d->entries[i].value;
        }
    }
    return NULL;
}

/* Supprime une clé et sa valeur */
int dict_remove(SimpleDict *d, const char *key) {
    if (!d || !key) return 0;
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            free(d->entries[i].key);
            free(d->entries[i].value);
            d->entries[i] = d->entries[d->count - 1];
            d->count--;
            return 1;
        }
    }
    return 0;
}