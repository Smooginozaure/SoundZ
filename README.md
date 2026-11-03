<img src="assets/icon.svg" width="64" height="64" alt="">

# SoundZ

*Version 1.0.0* — 📥 **[Télécharger](https://github.com/Smooginozaure/SoundZ/releases/latest)** · 📖 **[Guide](GUIDE.md)** · 🔌 **[Câbles virtuels](CABLES.md)** · ⚖️ **[Licence](LICENCE.md)**

![Windows 10 et 11](https://img.shields.io/badge/Windows-10%20et%2011-3e5068?style=flat-square) ![Portable](https://img.shields.io/badge/installation-aucune-3e5068?style=flat-square) ![Gratuit](https://img.shields.io/badge/prix-gratuit-3e5068?style=flat-square)

**SoundZ transforme le son de ton PC, en direct.** Il transpose la musique que tu écoutes, change ta voix avec un autotune ou un vocodeur, et ajoute un soundboard dans ton micro pour Discord, WhatsApp et les autres applis d'appel. Le tout tient dans un seul fichier d'environ 1 Mo : rien à installer, et un dossier à supprimer pour s'en débarrasser.

## Ce que fait SoundZ

### 🎵 Musique : change la tonalité de tout ce que tu écoutes
Spotify, YouTube, un jeu, un film… **toute la musique du PC** monte ou descend de **−4 à +4 demi-tons**, d'un simple curseur. Pratique pour chanter ou jouer d'un instrument dans ta tonalité, ou juste pour le plaisir.
- **Hauteur exacte**, à moins de 0,2 centième de demi-ton près.
- **Tempo, timbre et volume inchangés.** Les touches de volume continuent de marcher.
- **Environ 0,1 s de décalage**, imperceptible en musique.

### 🎤 Voix : autotune ou vocodeur sur ton micro
Ta voix passe par SoundZ avant d'arriver dans tes applis d'appel :
- **Autotune** : ta voix est ramenée sur les notes de la gamme choisie (tonalité, majeure, mineure, pentatoniques, blues…). Réglé sur « instantané », c'est l'effet robotique popularisé par T-Pain ; plus lent, il corrige en restant naturel.
- **Vocodeur** : voix de robot, avec un synthé qui suit ta voix. Les **accords** en font un chœur de robots.
- **Hauteur** de −12 à +12 demi-tons, **porte de bruit** contre le souffle, **préréglages** en un clic. Tu peux aussi t'entendre dans ton casque.

### 🔊 Soundboard : tes sons dans ton micro
Comme le soundboard de Discord, mais avec **n'importe quel son entendu sur ton PC** :
- **Capture** un passage d'une vidéo, d'un jeu ou d'un appel. Ou garde après coup **les 10 dernières secondes** avec **Ctrl+Maj+F12**, même en pleine partie.
- **Nettoyage automatique** : souffle retiré, silences coupés, volume égalisé. Les poignées sur la forme du son servent à garder le bon passage.
- **Un bouton par son**, avec un emoji, une image du PC, ou ✨ un dessin proposé par Claude. Chaque son a son **raccourci clavier** global.
- Le son part **dans ton micro**, mélangé à ta voix : tes amis l'entendent. Toi aussi, dans ton casque.

### ⚙️ Et aussi
- **Aucun réglage obligatoire.** SoundZ prend ton casque et ton micro habituels, met les câbles virtuels par défaut le temps de fonctionner, puis **remet tout comme avant** à la fermeture.
- Le menu ⚙ propose un **choix des appareils** (automatique ou précis), un **raccourci** dans le menu Démarrer et sur le Bureau, le **lancement avec Windows**, l'option « **réduire au lieu de fermer** », et la **réduction dans la zone de notification** (une petite icône près de l'horloge, au lieu de la barre des tâches).
- **Thème sombre et sobre**, teinte bleu-gris « Orage ».
- Le bouton **🪳**, à côté de ⚙, sert à **signaler un bug** ou **proposer une fonctionnalité** : il ouvre un ticket GitHub déjà rempli avec les informations utiles. Il faut un compte GitHub (gratuit).

## Comment ça marche

Windows ne laisse aucun programme modifier le son en route vers ta carte son ou venant de ton micro. SoundZ passe donc par des **câbles audio virtuels** :

```
Musique :  applis du PC  →  câble n° 1 (sas inaudible)  →  SoundZ : transposition  →  ton casque
Voix :     ton micro  →  SoundZ : effet + sons du soundboard  →  câble n° 2  ⇒  micro de Discord, WhatsApp…
```

Un seul câble suffit pour la voix et le soundboard. SoundZ reconnaît tout seul VB-CABLE, VB-CABLE A+B, Hi-Fi Cable et Voicemeeter, et répartit les câbles qu'il trouve. Le détail est dans **[CABLES.md](CABLES.md)**.

## Installation

1. Installe un **câble audio virtuel** : **[VB-CABLE](https://vb-audio.com/Cable/)** est gratuit et suffit pour la voix et le soundboard. Pour transposer aussi la musique, ajoute un deuxième câble : **Hi-Fi Cable** (gratuit) ou **VB-CABLE A+B** (sur don). Toutes les solutions, pas à pas : **[CABLES.md](CABLES.md)**. Redémarre, puis remets ton casque et ton micro par défaut dans les paramètres Son de Windows : à l'installation, chaque câble se met tout seul par défaut.
2. Télécharge **[la dernière version](https://github.com/Smooginozaure/SoundZ/releases/latest)**, puis décompresse-la dans un dossier à toi, par exemple `C:\Outils\SoundZ`.
3. Lance **`SoundZ.exe`**. Si Windows affiche « Windows a protégé votre ordinateur », clique sur **Informations complémentaires**, puis **Exécuter quand même**.
4. Dans Discord : périphérique d'entrée sur **Default**, et suppression du bruit sur **Aucune**.

Tout le reste est expliqué dans le **[guide](GUIDE.md)**.

## Un bug, une idée ?

Clique sur **🪳** dans SoundZ, ou ouvre directement un **[ticket GitHub](https://github.com/Smooginozaure/SoundZ/issues/new/choose)** : « Signaler un bug » ou « Proposer une fonctionnalité ».

## Vie privée

SoundZ fonctionne entièrement sur ton PC. Tes réglages et tes sons restent dans son dossier, et rien n'est envoyé nulle part. Seule exception, si tu la demandes : la fonction ✨ envoie le **nom** d'un son à Claude, via ton propre Claude Code, pour proposer une image.

## Licence

**SoundZ est gratuit** pour tous ceux qui le téléchargent depuis cette page GitHub, **sans le modifier**. La modification, la redistribution et l'usage commercial demandent l'autorisation de l'auteur. Conditions complètes : **[LICENCE.md](LICENCE.md)**.

SoundZ utilise [miniaudio](https://github.com/mackron/miniaudio) (domaine public ou MIT-0), ainsi que [signalsmith-stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) et [signalsmith-linear](https://github.com/Signalsmith-Audio/linear) (MIT). Les câbles virtuels (VB-CABLE, Hi-Fi Cable, Voicemeeter…) sont des logiciels de VB-Audio Software, non fournis avec SoundZ.

## Compiler

`build.bat` compile `dist\SoundZ.exe` avec le compilateur portable [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (fichier `ucrt-x86_64`, à décompresser dans `tools\llvm-mingw`). Le dossier `src` contient le code, `third_party` les bibliothèques.

© 2026 Andreas Berger
