#include <stdio.h>
#include <stdlib.h>

#define CASES 12
#define MAX_GRAINS 48

typedef struct {
    int cases[CASES];
    int score[2];
} Plateau;

void init_plateau(Plateau *plateau) {
    for (int i = 0; i < CASES; i++) {
        plateau->cases[i] = 4;
    }
    plateau->score[0] = 0;
    plateau->score[1] = 0;
}

int est_dans_camp_adverse(int joueur, int position) {
    return (joueur == 0 && position >= CASES / 2) || (joueur == 1 && position < CASES / 2);
}

int jouer_coup(Plateau *plateau, int joueur, int case_choisie) {
    int graines = plateau->cases[case_choisie];
    plateau->cases[case_choisie] = 0;
    int position = case_choisie;

    while (graines > 0) {
        position = (position + 1) % CASES;
        if (position != case_choisie) {
            plateau->cases[position]++;
            graines--;
        }
    }

    while (est_dans_camp_adverse(joueur, position) && (plateau->cases[position] == 2 || plateau->cases[position] == 3)) {
        plateau->score[joueur] += plateau->cases[position];
        plateau->cases[position] = 0;
        position--;
    }

    return plateau->score[joueur] >= MAX_GRAINS / 2;
}

void afficher_plateau(Plateau *plateau) {
    for (int i = 11; i >= 6; i--) {
        printf("%2d ", plateau->cases[i]);
    }
    printf("\n   ");
    for (int i = 0; i < 6; i++) {
        printf("%2d ", plateau->cases[i]);
    }
    printf("\nScore Joueur 1: %d | Score Joueur 2: %d\n", plateau->score[0], plateau->score[1]);
}

int main() {
    Plateau plateau;
    int joueur = 0, case_choisie;

    init_plateau(&plateau);

    while (1) {
        afficher_plateau(&plateau);
        printf("Joueur %d, choisissez une case (0-5 pour joueur 1, 6-11 pour joueur 2): ", joueur + 1);
        scanf("%d", &case_choisie);

        if (case_choisie < 0 || case_choisie >= CASES || plateau.cases[case_choisie] == 0 || (joueur == 0 && case_choisie >= 6) || (joueur == 1 && case_choisie < 6)) {
            printf("Coup invalide.\n");
            continue;
        }

        if (jouer_coup(&plateau, joueur, case_choisie)) {
            afficher_plateau(&plateau);
            printf("Joueur %d gagne !\n", joueur + 1);
            break;
        }

        joueur = 1 - joueur;
    }

    return 0;
}
