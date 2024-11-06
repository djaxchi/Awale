#ifndef AWALE_H
#define AWALE_H

#include <stdio.h>

#define CASES 12
#define MAX_GRAINS 48

typedef struct {
    int cases[CASES];
    int score[2];
} Plateau;

// Initialise le plateau de jeu Awalé avec 4 graines dans chaque case
void init_plateau(Plateau *plateau);

// Vérifie si une position donnée est dans le camp adverse
int est_dans_camp_adverse(int joueur, int position);

// Vérifie si l'adversaire est en famine (toutes ses cases sont vides)
int check_famine(Plateau *plateau, int joueur);

// Enregistre l'état actuel du plateau dans un fichier
void enregistrer_plateau(FILE *fichier, Plateau *plateau);

// Joue un coup pour un joueur donné à une case choisie et met à jour le plateau
int jouer_coup(Plateau *plateau, int joueur, int case_choisie);

// Affiche l'état actuel du plateau dans la console
void afficher_plateau(Plateau *plateau);

#endif /* AWALE_H */
