# Guide de SoundZ

![Version 1.1.0](https://img.shields.io/badge/version-1.1.0-3e5068?style=flat-square) ![Windows 10 et 11](https://img.shields.io/badge/Windows-10%20et%2011-3e5068?style=flat-square) ![Portable](https://img.shields.io/badge/installation-aucune-3e5068?style=flat-square)

SoundZ est un petit utilitaire Windows, en un seul fichier, avec trois onglets :

| Onglet | Ce qu'il fait |
|---|---|
| 🎵 **Musique** | Transpose **toute la musique du PC** (Spotify, YouTube, jeux…) de −4 à +4 demi-tons, en direct. |
| 🎤 **Voix (micro)** | Ajoute un **autotune** ou un **vocodeur** à ta voix, en direct, pour Discord, WhatsApp… |
| 🔊 **Soundboard** | Capture des sons du PC et les range sous des boutons. Un clic ou un raccourci clavier, et le son part dans ton micro. |

---

## 1. Installation (une seule fois)

| | Étape |
|:-:|---|
| **1** | Installe **[VB-CABLE](https://vb-audio.com/Cable/)** (gratuit) : il suffit pour la voix et le soundboard. Pour transposer aussi la musique, ajoute un 2e câble : **Hi-Fi Cable** (gratuit) ou **VB-CABLE A+B** (sur don). Toutes les solutions, pas à pas : **[CABLES.md](https://github.com/Smooginozaure/SoundZ/blob/main/CABLES.md)**. Redémarre ensuite le PC. |
| **2** | ⚠️ À l'installation, chaque câble se met tout seul en sortie et en micro par défaut. Dans les **paramètres Son** de Windows, remets ton casque (ou tes enceintes) en sortie et ton micro en entrée. |
| **3** | Décompresse l'archive de SoundZ dans un dossier à toi, par exemple `C:\Outils\SoundZ`. |
| **4** | Lance **`SoundZ.exe`**. Si Windows affiche « Windows a protégé votre ordinateur », clique sur **Informations complémentaires**, puis **Exécuter quand même**. |

> [!TIP]
> SoundZ n'installe rien : pas de menu Démarrer ni de registre, sauf si tu le demandes dans le menu ⚙. Tout reste dans son dossier.

## 2. Ce qui se passe au lancement

- SoundZ prend **ton casque et ton micro**, c'est-à-dire les appareils par défaut de Windows à ce moment-là.
- Il met ensuite les câbles virtuels par défaut, pour que la musique et la voix passent par lui.
- **À la fermeture, il remet tout comme avant.** Il le fait aussi au lancement suivant s'il a été coupé brutalement.

## 3. Les trois onglets

### 🎵 Musique
- Bouge le curseur pour transposer, de −4 à +4 demi-tons. La molette et les flèches marchent aussi.
- **Remettre à 0** revient au son d'origine.
- **Désactiver** rend le son à Windows, sans fermer SoundZ.

### 🎤 Voix (micro)
- **Sans effet**, **Autotune** ou **Vocodeur**.
- Règle la **tonalité**, la **gamme**, la **vitesse de correction** (« instantanée » = effet robotique), l'**intensité**, la **hauteur** et la **porte de bruit**.
- Les préréglages **Naturel**, **T-Pain**, **Robot** et **Chœur** s'appliquent en un clic.
- « **Entendre ma voix (écho)** » te fait entendre ta voix dans ton casque. C'est facultatif : coupe-le si tu ne veux entendre que les sons du soundboard.

### 🔊 Soundboard
- **+ Nouveau son** : clique sur **● Capturer**, puis lance la vidéo. La capture démarre dès que le son arrive.
- **Ctrl+Maj+F12** garde les **10 dernières secondes** du PC, même en jeu. Un bouton jaune **« À trier »** apparaît : clique dessus, puis garde le bon passage avec les poignées.
- Le son est nettoyé tout seul : souffle retiré, silences coupés, volume égalisé.
- Choisis une **image** (un emoji, une image du PC, ou ✨ une image proposée par Claude si Claude Code est installé) et un **raccourci** (Ctrl+Maj+1, 2, 3…).
- **Ctrl+Maj+0** ou **■ Stop** arrête tous les sons. **Clic droit** sur un bouton : modifier, déplacer, supprimer.
- « **Entendre les sons du soundboard** » te fait entendre tes sons dans ton casque, sans ta voix.
- Un nom trop long est coupé ? **Élargis la fenêtre** en tirant son bord droit : les boutons s'agrandissent avec elle. SoundZ retient la largeur.
- **🔎 Myinstants** : sous les boutons, tape le nom d'un son (par exemple « bruh »), puis **Chercher** ou Entrée. Les résultats du site [myinstants.com](https://www.myinstants.com) s'affichent, 8 par 8. **▶** fait écouter un son dans ton casque seulement, et **+ Ajouter** le range dans ton soundboard, avec son nom sur le site et le prochain raccourci libre. Clic droit → Modifier pour changer son nom ou son image. Aucune IA n'est utilisée. Ces sons appartiennent à leurs auteurs : garde-les pour ton usage personnel.

### 🎮 Overlay en jeu
- **Ctrl+Maj+F11** affiche un petit panneau par-dessus ton jeu. **Il reste affiché** pendant que tu joues, jusqu'au prochain Ctrl+Maj+F11 (ou ✕). Le raccourci se change dans l'onglet Soundboard.
- Tout se fait **à la souris**. **Clic sur un son** : il part dans ton micro. **Clic sur une capture « À trier »** : tu gardes le bon passage avec les poignées, tu règles le volume et le nettoyage, tu écoutes, puis **✓ Garder** ou **Supprimer**.
- Un son gardé en jeu apparaît en jaune, **« ✎ Son 7 »**. Plus tard, sur le bureau : **clic droit → Modifier** pour lui donner un nom et une image.
- Cliquer dans l'overlay **ne fait pas sortir du jeu** : ton clavier commande toujours ton personnage. Pour le déplacer, attrape-le par son bandeau du haut.
- Quand l'overlay est masqué, **Ctrl+Maj+F12** affiche une petite confirmation en haut de l'écran.
- L'overlay s'affiche en mode **plein écran fenêtré** (« Fenêtré (plein écran) », « Borderless »), et dans le plein écran de la plupart des jeux récents. En **plein écran exclusif**, il reste invisible, mais les raccourcis marchent toujours.
- L'overlay ne touche pas au jeu : rien n'est injecté ni lu dans le jeu. C'est une fenêtre ordinaire de SoundZ, posée au-dessus.

### 📱 Télécommande sur téléphone
- Dans **⚙ → Télécommande sur téléphone**, active la télécommande. Un **QR code** s'affiche.
- La première fois, Windows demande d'autoriser SoundZ sur le réseau : coche **Réseaux privés**, puis **Autoriser**.
- **Scanne le QR code** avec l'appareil photo du téléphone, qui doit être sur **la même box** que le PC. La télécommande s'ouvre dans le navigateur.
- Garde-la sur l'écran d'accueil, comme une appli. Sur **iPhone** : Partager → **« Sur l'écran d'accueil »**. Sur **Android** : ⋮ → **« Ajouter à l'écran d'accueil »**.
- Trois onglets, comme sur le PC :
  - **Soundboard** : un appui sur un bouton, et le son part dans ton micro. Il y a aussi **■ Stop**, **✂ 10 dernières s** et le volume des sons.
  - **Voix** : activer ou couper la voix, l'effet, les préréglages et tous les réglages.
  - **Musique** : la hauteur, de −4 à +4 demi-tons.
- Tout ce que tu changes sur le téléphone change aussi sur le PC, et inversement.
- **Nouveau code** déconnecte les téléphones déjà reliés : ils devront scanner le nouveau QR code.
- Rien ne passe par Internet. Le code secret du QR code empêche les autres appareils du Wi-Fi d'y accéder.

## 4. Régler Discord (et les autres applis)

| Réglage de Discord (*Paramètres → Voix et vidéo*) | Valeur |
|---|---|
| Périphérique d'entrée | **Default** : Discord suit alors SoundZ tout seul |
| Périphérique de sortie | Ton casque |
| Suppression du bruit | **Aucune**, sinon Discord coupe les sons du soundboard |

Pour les autres applis : choisis le micro **« Par défaut »**, ou la sortie de ton câble (**« CABLE-A Output »**, **« CABLE Output »**…). L'onglet Voix affiche lequel.

## 5. Le menu ⚙

- **Télécommande sur téléphone…** : le QR code pour commander SoundZ depuis ton téléphone (voir plus haut).
- **Appareils…** : choisis la sortie et le micro, en **Automatique** (appareils par défaut de Windows) ou un appareil précis. Tu y vois aussi quel câble virtuel sert à quoi, et avec un seul câble, s'il va à la voix ou à la musique.
- **Raccourci dans le menu Démarrer**, **Icône sur le Bureau**, **Lancer SoundZ avec Windows**.
- **Réduire au lieu de fermer** : la croix réduit SoundZ au lieu de le fermer. **Quitter SoundZ** ferme pour de bon.
- **Réduire dans la zone de notification** : réduit, SoundZ quitte la barre des tâches et devient une petite icône « SZ » près de l'horloge. **Clic** sur l'icône : la fenêtre revient. **Clic droit** : Ouvrir ou Quitter. Avec « Lancer SoundZ avec Windows », il y démarre directement.

## 6. En cas de problème

| Problème | Solution |
|---|---|
| Plus de son sur le PC | Paramètres Son de Windows → choisis ton casque ou tes enceintes en sortie. |
| Le micro ne marche plus dans une appli | Paramètres Son → remets ton vrai micro en entrée, ou choisis « Par défaut » dans l'appli. |
| « Aucun câble virtuel » ou « Un seul câble virtuel » | Installe un câble (étape 1, ou [toutes les solutions](https://github.com/Smooginozaure/SoundZ/blob/main/CABLES.md)), puis redémarre. Avec un seul câble, choisis à quoi il sert : ⚙ → Appareils. |
| Mauvais casque ou mauvais micro | ⚙ → **Appareils…** → choisis le bon. |
| Les amis n'entendent pas les sons | Onglet Voix : la voix doit être active. Dans Discord : suppression du bruit sur « Aucune ». |
| L'overlay n'apparaît pas en jeu | Dans les options graphiques du jeu, choisis le mode **plein écran fenêtré** (« Fenêtré (plein écran) », « Borderless »). |
| Le téléphone n'arrive pas à se connecter | Le téléphone doit être sur le Wi-Fi de **la même box** que le PC, pas en 4G/5G. Dans Windows, ce réseau doit être **privé** : Paramètres → Réseau et Internet → Wi-Fi (ou Ethernet) → ton réseau → **Réseau privé**. Si tu as refusé l'autorisation de Windows : Pare-feu Windows → « Autoriser une application » → coche SoundZ pour les réseaux privés. Les Wi-Fi publics ou d'entreprise bloquent souvent les échanges entre appareils. |
| La télécommande ne marche plus | L'adresse du PC a pu changer (box redémarrée) : rouvre ⚙ → Télécommande sur téléphone et scanne le nouveau QR code. |
| L'écran du téléphone se met en veille | Normal : un navigateur ne peut pas le garder allumé. Allonge le délai de verrouillage pendant tes parties. |

**Un autre problème, ou une idée ?** Clique sur **🪳**, à côté de ⚙, puis sur **Signaler un bug** ou **Proposer une fonctionnalité**. Un ticket GitHub s'ouvre, déjà rempli avec ta version de SoundZ, de Windows et tes câbles virtuels. Il te reste à décrire le problème. Il faut un compte GitHub, gratuit.

## 7. Désinstaller

Dans **⚙**, décoche le menu Démarrer, le Bureau et le lancement avec Windows. Clique sur **Quitter SoundZ**, puis supprime le dossier de SoundZ. C'est tout.

---

SoundZ est gratuit pour tous ceux qui le téléchargent sur **[github.com/Smooginozaure/SoundZ](https://github.com/Smooginozaure/SoundZ)**, sans le modifier. Conditions complètes : [LICENCE.md](LICENCE.md).
