#ifndef USERS_H
#define USERS_H

#include "dict.h"

/**
 * Save all users to a text file
 */
int users_save_all(const char *filename, SimpleDict *users_dict);

/**
 * Load users from a text file into the dictionary
 */
int users_load_all(const char *filename, SimpleDict *users_dict);

#endif