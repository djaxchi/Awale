Voici un exemple de **README.md** pour votre projet, détaillant les fonctionnalités, les étapes pour compiler et exécuter le programme :

---

# Awalé Multiplayer Game Server

Ce projet implémente un serveur de jeu multijoueur pour l'Awalé, avec des fonctionnalités avancées telles que le matchmaking, les observateurs, la gestion des bios des joueurs, et un système d'amis.

## Fonctionnalités Implémentées

### Gestion des joueurs
- **Connexion des joueurs** : Les joueurs peuvent se connecter au serveur avec un nom unique.
- **Déconnexion propre** : Les joueurs peuvent se déconnecter, et leur état est réinitialisé.
- **Liste des joueurs connectés** : Les joueurs peuvent consulter la liste des utilisateurs actuellement connectés.

### Jeu multijoueur
- **Matchmaking** :
  - Envoi de requêtes de jeu à d'autres joueurs.
  - Acceptation ou refus des demandes de jeu.
- **Jeu en temps réel** :
  - Tour par tour avec un plateau de jeu interactif.
  - Système pour quitter une partie en cours.
- **Spectateurs** :
  - Les joueurs peuvent observer des parties en cours.
  - Mode "amis uniquement" pour limiter les spectateurs.
  
### Système d'amis
- **Gestion des amis** :
  - Envoi et réception de demandes d'amis.
  - Acceptation ou refus des demandes.
  - Affichage de la liste d'amis.
- **Vérification d'amitié** : Les joueurs peuvent limiter la liste des observateurs à leur liste d’amis.

### Système de bios
- Les joueurs peuvent :
  - Définir ou mettre à jour leur biographie (limite de 10 lignes, caractères ASCII uniquement).
  - Consulter la biographie d'autres joueurs.

### Historique et classement
- **Système ELO** : Les joueurs gagnent ou perdent des points ELO en fonction de leurs résultats.
- **Top joueurs** : Affichage des 5 meilleurs joueurs classés.
- **Historique des parties** :
  - Sauvegarde automatique de l'état des parties.
  - Relecture des parties sauvegardées.

---

## Instructions pour compiler et exécuter

### Prérequis
- Création des dossiers suivants dans le répertoire du projet :
  - `Database`
  - `Database/Games`

### Compilation
Pour compiler le programme, utilisez la commande suivante dans le terminal :

    make

Cette comande va compiler un serveur et 4 clients

Pour nettoyer l'arborescence, utilisez la commande make clean. 

### Exécution
1. Lancez le serveur en exécutant la commande suivante :
   ./server

2. Lancez les clients (de 1 à 4) en utilisant la commande suivante :
    ./client1 <ip du serveur> <nom du joueur>
