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

int check_famine(Plateau *plateau, int joueur) {
    int debut = (joueur == 0) ? 0 : CASES / 2;
    int fin = (joueur == 0) ? CASES / 2 : CASES;
    
    for (int i = debut; i < fin; i++) {
        if (plateau->cases[i] > 0) {
            return 0;
        }
    }
    return 1;
}

void enregistrer_plateau(FILE *fichier, Plateau *plateau) {
    fprintf(fichier, "\n  --- Plateau de jeu Awalé ---\n\n");
    fprintf(fichier, "      +---+---+---+---+---+---+\n      ");
    for (int i = 11; i >= 6; i--) {
        fprintf(fichier, " %2d ", plateau->cases[i]);
    }
    fprintf(fichier, "\nJ2    +---+---+---+---+---+---+\nJ1    +---+---+---+---+---+---+\n      ");
    for (int i = 0; i < 6; i++) {
        fprintf(fichier, " %2d ", plateau->cases[i]);
    }
    fprintf(fichier, "\n      +---+---+---+---+---+---+\n");
    fprintf(fichier, "Score Joueur 1: %d | Score Joueur 2: %d\n", plateau->score[0], plateau->score[1]);
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

    return plateau->score[joueur] > MAX_GRAINS / 2;
}

void afficher_plateau(Plateau *plateau) {
    printf("\n  --- Plateau de jeu Awalé ---\n");
    printf("     pour quitter, entrez 0   ");
    printf("\n      +---+---+---+---+---+---+\n      ");
    for (int i = 11; i >= 6; i--) {
        printf(" %2d ", plateau->cases[i]);
    }
    printf("\nJ2    +---+---+---+---+---+---+\nJ1    +---+---+---+---+---+---+\n      ");
    for (int i = 0; i < 6; i++) {
        printf(" %2d ", plateau->cases[i]);
    }
    printf("\n      +---+---+---+---+---+---+\n");
    printf("Score Joueur 1: %d | Score Joueur 2: %d\n", plateau->score[0], plateau->score[1]);
}

int main() {
    Plateau plateau;
    int joueur = 0, case_choisie;

    init_plateau(&plateau);
    
    FILE *fichier = fopen("partie.txt", "w");
    if (fichier == NULL) {
        printf("Erreur lors de l'ouverture du fichier.\n");
        return 1;
    }

    fprintf(fichier, "Début de la partie\n");
    enregistrer_plateau(fichier, &plateau);

    while (1) {
        afficher_plateau(&plateau);
        fprintf(fichier, "\nTour du joueur %d\n", joueur + 1);
        
        printf("Joueur %d, choisissez une case (1-6): ", joueur + 1);
        scanf("%d", &case_choisie);
        if(case_choisie == 0) {
            break;
        }
        case_choisie--; 

        int index = case_choisie + (joueur == 1 ? 6 : 0);

        if (check_famine(&plateau, 1 - joueur)) {
            int derniere_position = (case_choisie + plateau.cases[case_choisie]) % CASES;
            if (!(derniere_position >= CASES / 2 * (1 - joueur) && derniere_position < CASES / 2 * (2 - joueur))) {
                printf("Coup invalide : vous devez nourrir l'adversaire.\n");
                fprintf(fichier, "Coup invalide : famine détectée pour l'adversaire\n");
                continue;
            }
        }

        if (case_choisie < 0 || case_choisie >= 6 || plateau.cases[index] == 0) {
            printf("Coup invalide.\n");
            fprintf(fichier, "Coup invalide\n");
            continue;
        }

        if (jouer_coup(&plateau, joueur, index)) {
            afficher_plateau(&plateau);
            enregistrer_plateau(fichier, &plateau);
            printf("Joueur %d gagne !\n", joueur + 1);
            fprintf(fichier, "Joueur %d gagne !\n", joueur + 1);
            break;
        }

        enregistrer_plateau(fichier, &plateau);
        joueur = 1 - joueur;
    }

    fprintf(fichier, "Fin de la partie\n");
    fclose(fichier);
    return 0;
}
