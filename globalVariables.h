#ifndef GLOBALS_H
#define GLOBALS_H

extern int serverPort;
extern int INITIAL_CAPACITY;
extern int LOAD_FACTOR_THRESHOLD;

/* Commandes de base */
#define LOGIN_CMD "@login"        /* Format: "@login username" */
#define MESSAGE_CMD "@message"    /* Format: "@message &destinataire message" */

/* Commandes pour les salles de chat */
#define CREATEROOM_CMD "@createroom"  /* Format: "@createroom nom_salle max_membres" */
#define JOINROOM_CMD "@joinroom"      /* Format: "@joinroom nom_salle" */
#define LEAVEROOM_CMD "@leaveroom"    /* Format: "@leaveroom nom_salle" */
#define LISTROOMS_CMD "@listrooms"    /* Format: "@listrooms" */
#define ROOMSG_CMD "@roomsg"          /* Format: "@roomsg nom_salle message" */
#define LISTMEMBERS_CMD "@listmembers" /* Format: "@listmembers nom_salle" */

/* Configuration des salles de chat */
#define MAX_ROOMS 20              /* Nombre maximum de salles */
#define MAX_ROOM_NAME_LENGTH 50   /* Longueur maximum du nom d'une salle */

#endif