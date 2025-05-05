#include "dict.h"
#include <stdlib.h>
#include <string.h>

/* Double la capacité interne du dictionnaire */
static void dict_resize(SimpleDict *d) {
    size_t new_capacity = d->capacity * 2;
    Entry *new_entries = realloc(d->entries, new_capacity * sizeof(Entry));
    if (!new_entries) return;  // en cas d'échec on reste sur l'ancien
    d->entries = new_entries;
    d->capacity = new_capacity;
}

SimpleDict *dict_create(void) {
    SimpleDict *d = malloc(sizeof(SimpleDict));
    if (!d) return NULL;
    d->count = 0;
    d->capacity = INITIAL_CAPACITY;
    d->entries = calloc(d->capacity, sizeof(Entry));
    return d;
}

void dict_free(SimpleDict *d) {
    if (!d) return;
    for (size_t i = 0; i < d->count; i++) {
        free(d->entries[i].key);
        free(d->entries[i].value);
    }
    free(d->entries);
    free(d);
}

int dict_insert(SimpleDict *d, const char *key, const char *value) {
    // redimensionnement si load factor > threshold
    if ((double)(d->count + 1) / d->capacity > LOAD_FACTOR_THRESHOLD) {
        dict_resize(d);
    }
    // insertion en fin, pas de mise à jour si déjà présent
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            return 0; // existant
        }
    }
    d->entries[d->count].key = strdup(key);
    d->entries[d->count].value = strdup(value);
    d->count++;
    return 1;
}

const char *dict_get(SimpleDict *d, const char *key) {
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            return d->entries[i].value;
        }
    }
    return NULL;
}

int dict_remove(SimpleDict *d, const char *key) {
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->entries[i].key, key) == 0) {
            free(d->entries[i].key);
            free(d->entries[i].value);
            // decaler vers l'avant
            for (size_t j = i; j + 1 < d->count; j++) {
                d->entries[j] = d->entries[j + 1];
            }
            d->count--;
            return 1;
        }
    }
    return 0;
}