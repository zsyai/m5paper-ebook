#ifndef EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_USER_GUIDE_TEXT_FR_H_
#define EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_USER_GUIDE_TEXT_FR_H_
// Built-in user guide, French.
constexpr char kUserGuideTextFr[] = R"====([Fonctions]

1. Livres : liste les livres .txt et .epub du dossier /books de la carte SD avec leur progression (TXT en UTF-8, UTF-16 LE/BE ou GBK/GB18030 ; EPUB avec titre et pages d'illustration). Touchez le tri au centre de la barre du haut pour trier par lecture récente, titre, progression ou date d'ajout. Un appui long sur un livre permet d'effacer sa progression ou de le supprimer. Glissez ou tournez la molette pour changer de page ; un appui court sur la molette rafraîchit l'écran. En bas : Transfert, Réglages, Veille.

2. Transfert sans fil : l'appareil crée un point d'accès WiFi. Connectez-y un téléphone ou un ordinateur, puis ouvrez dans le navigateur l'adresse IP affichée pour envoyer et gérer livres (/books), polices (/fonts, .ttf/.otf/.bin) et fonds d'écran (/images, 540x960 .png/.jpg).

3. Réglages de lecture (depuis les Réglages ou le menu de lecture), sur deux pages, à alterner avec le bouton en bas à droite :
3.1 Mise en page : police et taille du texte (20 à 60 px pour les polices TTF, touchez le nombre pour le saisir), interligne (1.0 à 3.0), marges (0 à 80 px), alinéa, réglure, quatre orientations de lecture, avec aperçu en direct.
3.2 Commandes : rafraîchissement complet (toutes les N pages, pour effacer les traces), page auto (activation et délai en secondes), zones tactiles, glissements, sens de la molette.
3.3 Zones tactiles : 3 ou 9 zones ; touchez une case du schéma pour changer son action (page précédente, page suivante, menu, rafraîchir, aucune).
3.4 Glissements : action pour chaque direction (gauche, droite, haut, bas).

4. Réglages système :
4.1 Police menus : police intégrée 30 px ou police TTF/OTF du dossier /fonts.
4.2 Mode veille : profonde ou légère (voir plus bas).
4.3 Langue : langue de l'interface, 简体中文 (chinois simplifié) ou Français.
4.4 Fond d'écran : active ou non l'image de veille et la choisit dans /images.
4.5 Nettoyage : supprime les données des livres effacés (.cfg/.idx/.epc/.toc) ou le cache des polices TTF inutilisées.

5. Veille et réveil : touchez Veille dans la bibliothèque, les réglages ou le menu de lecture. Au réveil, vous retrouvez la page où vous étiez.
5.1 Veille profonde (par défaut) : l'appareil est éteint, c'est le plus économe. Appuyez 3 s sur la molette pour le rallumer.
5.2 Veille légère : l'appareil reste alimenté et se réveille dès qu'on appuie ou tourne la molette, mais consomme davantage ; après 24 h, il passe en veille profonde.

[En lecture]

1. Tourner les pages : par défaut, touchez le tiers gauche de l'écran pour la page précédente et le tiers droit pour la suivante ; glissez vers la gauche pour avancer, vers la droite pour reculer ; la molette fonctionne aussi. Tout cela se change dans Commandes.

2. Menu et sommaire : touchez le tiers central ou glissez vers le bas pour ouvrir le menu : page et progression, retour à la bibliothèque, sommaire (aller à un chapitre, une page ou un pourcentage), réglages de lecture, veille. Touchez le haut de l'écran pour fermer le menu.

3. Page auto : une fois activée dans Commandes, la page tourne toute seule selon le délai choisi et s'arrête à la fin du livre ; toute action relance le compte à rebours.

4. Rafraîchissement forcé : un appui court sur la molette (en lecture, dans la bibliothèque ou le sommaire) rafraîchit tout l'écran pour effacer les traces.

[Problèmes]

Appareil bloqué : s'il ne répond plus (même après 3 s d'appui sur la molette), appuyez sur le bouton Reset au dos, puis rallumez-le.
)====";

#endif  // EXPERIMENTAL_USERS_ZHENGSHUYU_READER_INCLUDE_UI_PAGES_USER_GUIDE_TEXT_FR_H_
