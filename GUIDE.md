# Guide de SoundZ

![Version 1.0.0](https://img.shields.io/badge/version-1.0.0-3e5068?style=flat-square) ![Windows 10 et 11](https://img.shields.io/badge/Windows-10%20et%2011-3e5068?style=flat-square) ![Portable](https://img.shields.io/badge/installation-aucune-3e5068?style=flat-square)

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
- « M'entendre dans le casque » est facultatif.

### 🔊 Soundboard
- **+ Nouveau son** : clique sur **● Capturer**, puis lance la vidéo. La capture démarre dès que le son arrive.
- **Ctrl+Maj+F12** garde les **10 dernières secondes** du PC, même en jeu. Un bouton jaune **« À trier »** apparaît : clique dessus, puis garde le bon passage avec les poignées.
- Le son est nettoyé tout seul : souffle retiré, silences coupés, volume égalisé.
- Choisis une **image** (un emoji, une image du PC, ou ✨ une image proposée par Claude si Claude Code est installé) et un **raccourci** (Ctrl+Maj+1, 2, 3…).
- **Ctrl+Maj+0** ou **■ Stop** arrête tous les sons. **Clic droit** sur un bouton : modifier, déplacer, supprimer.

## 4. Régler Discord (et les autres applis)

| Réglage de Discord (*Paramètres → Voix et vidéo*) | Valeur |
|---|---|
| Périphérique d'entrée | **Default** : Discord suit alors SoundZ tout seul |
| Périphérique de sortie | Ton casque |
| Suppression du bruit | **Aucune**, sinon Discord coupe les sons du soundboard |

Pour les autres applis : choisis le micro **« Par défaut »**, ou la sortie de ton câble (**« CABLE-A Output »**, **« CABLE Output »**…). L'onglet Voix affiche lequel.

## 5. Le menu ⚙

- **Appareils…** : choisis la sortie et le micro, en **Automatique** (appareils par défaut de Windows) ou un appareil précis. Tu y vois aussi quel câble virtuel sert à quoi, et avec un seul câble, s'il va à la voix ou à la musique.
- **Raccourci dans le menu Démarrer**, **Icône sur le Bureau**, **Lancer SoundZ avec Windows**.
- **Réduire au lieu de fermer** : la croix réduit SoundZ au lieu de le fermer. **Quitter SoundZ** ferme pour de bon.

## 6. En cas de problème

| Problème | Solution |
|---|---|
| Plus de son sur le PC | Paramètres Son de Windows → choisis ton casque ou tes enceintes en sortie. |
| Le micro ne marche plus dans une appli | Paramètres Son → remets ton vrai micro en entrée, ou choisis « Par défaut » dans l'appli. |
| « Aucun câble virtuel » ou « Un seul câble virtuel » | Installe un câble (étape 1, ou [toutes les solutions](https://github.com/Smooginozaure/SoundZ/blob/main/CABLES.md)), puis redémarre. Avec un seul câble, choisis à quoi il sert : ⚙ → Appareils. |
| Mauvais casque ou mauvais micro | ⚙ → **Appareils…** → choisis le bon. |
| Les amis n'entendent pas les sons | Onglet Voix : la voix doit être active. Dans Discord : suppression du bruit sur « Aucune ». |

## 7. Désinstaller

Dans **⚙**, décoche le menu Démarrer, le Bureau et le lancement avec Windows. Clique sur **Quitter SoundZ**, puis supprime le dossier de SoundZ. C'est tout.

---

SoundZ est gratuit pour tous ceux qui le téléchargent sur **[github.com/Smooginozaure/SoundZ](https://github.com/Smooginozaure/SoundZ)**, sans le modifier. Conditions complètes : [LICENCE.md](LICENCE.md).
