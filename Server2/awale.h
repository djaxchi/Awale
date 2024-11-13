#ifndef AWALE_H
#define AWALE_H

#include <stdio.h>

#define CASES 12
#define MAX_GRAINS 48

typedef struct {
    int cases[CASES];
    int score[2];
} Plateau;

void init_plateau(Plateau *plateau);

int est_dans_camp_adverse(int joueur, int position);

int check_famine(Plateau *plateau, int joueur);

void enregistrer_plateau(FILE *fichier, Plateau *plateau);

int jouer_coup(Plateau *plateau, int joueur, int case_choisie);

const char* afficher_plateau(Plateau *plateau);

#endif /* AWALE_H */
