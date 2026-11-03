# Câbles virtuels : toutes les solutions

SoundZ a besoin de **câbles audio virtuels** : Windows n'en fournit aucun. Ce guide présente toutes les façons d'en avoir, **y compris sans acheter VB-CABLE A+B**.

## Pourquoi des câbles ?

Un câble virtuel est une fausse carte son : ce qui est joué dans son entrée (*Input*) ressort dans sa sortie (*Output*), qui apparaît comme un micro.

| SoundZ s'en sert pour… | Combien de câbles |
|---|---|
| 🎤 La **voix** et le **soundboard** : ta voix transformée part dans le câble, et Discord écoute le câble comme un micro | **1 câble** |
| 🎵 La **musique** : les applis jouent dans le câble, SoundZ transpose et renvoie vers ton casque | **1 câble de plus** |

**SoundZ choisit tout seul** quel câble sert à quoi, selon ceux qu'il trouve. Tu peux voir et changer ce choix dans ⚙ → **Appareils**.

## Les solutions en un coup d'œil

| Solution | Prix | 🎤 Voix + soundboard | 🎵 Musique | Facilité |
|---|---|:-:|:-:|---|
| **1. VB-CABLE seul** | Gratuit | ✅ | ➖ *(ou la musique à la place)* | ⭐⭐⭐ très simple |
| **2. VB-CABLE + Hi-Fi Cable** | Gratuit | ✅ | ✅ | ⭐⭐⭐ simple |
| **3. VB-CABLE + Voicemeeter** | Gratuit | ✅ | ✅ | ⭐⭐ un réglage dans Voicemeeter, qui doit rester ouvert |
| **4. VB-CABLE + VB-CABLE A+B** | Don (environ 15 $) | ✅ | ✅ | ⭐⭐⭐ simple |
| 5. Virtual Audio Cable (VAC) | Payant | ✅ | ✅ | ⭐ réglages à la main |

> [!TIP]
> **Le conseil :** commence par la **solution 1** (VB-CABLE, gratuit). Si tu veux aussi transposer la musique, ajoute **Hi-Fi Cable** (solution 2), gratuit lui aussi.

> [!IMPORTANT]
> **Après chaque installation de câble :**
> - **redémarre** le PC ;
> - ouvre les **paramètres Son** de Windows et remets **ton casque** en sortie et **ton micro** en entrée. Chaque câble se met tout seul par défaut quand on l'installe.

---

## Solution 1 — VB-CABLE seul (gratuit)

Un câble : il sert à la **voix et au soundboard**. Tu peux le donner à la **musique** à la place.

1. Va sur **[vb-audio.com/Cable](https://vb-audio.com/Cable/)** et télécharge **VB-CABLE Driver Pack** (Windows).
2. Décompresse le fichier `.zip`, fais un clic droit sur **`VBCABLE_Setup_x64.exe`**, puis **Exécuter en tant qu'administrateur**, et **Install Driver**.
3. Redémarre, puis remets ton casque et ton micro par défaut (voir l'encadré ci-dessus).
4. Lance SoundZ. L'onglet **Voix** utilise le câble : dans Discord, mets le périphérique d'entrée sur **Default** (ou **CABLE Output**).
5. L'onglet **Musique** affiche « Un seul câble virtuel ». Pour donner le câble à la musique à la place : ⚙ → **Appareils** → **Câbles virtuels** → « Il sert à la musique ».

VB-CABLE est un *donationware* : gratuit, avec un don possible si tu le trouves utile.

## Solution 2 — VB-CABLE + Hi-Fi Cable (gratuits)

Deux câbles gratuits, tous deux de VB-Audio : **tout fonctionne**.

1. Installe **VB-CABLE** (solution 1, étapes 1 à 3).
2. Sur la même page **[vb-audio.com/Cable](https://vb-audio.com/Cable/)**, télécharge **Hi-Fi Cable & ASIO Bridge**. Décompresse-le, lance son installateur en **administrateur**, puis redémarre.
3. Remets ton casque et ton micro par défaut.
4. Lance SoundZ. Il répartit tout seul les câbles :
   - la **voix** sur **CABLE Input**, que Discord écoute comme **CABLE Output** ;
   - la **musique** sur **Hi-Fi Cable**.

   Pour vérifier : ⚙ → Appareils.

Hi-Fi Cable est gratuit pour les particuliers. Son installateur date un peu : si Windows refusait de l'installer, passe à la solution 3 ou 4.

## Solution 3 — VB-CABLE + Voicemeeter (gratuits)

Voicemeeter est une table de mixage virtuelle gratuite de VB-Audio. Elle contient aussi un câble, qui peut servir à la voix. VB-CABLE garde la musique.

1. Installe **VB-CABLE** (solution 1, étapes 1 à 3).
2. Télécharge **Voicemeeter** sur **[vb-audio.com/Voicemeeter](https://vb-audio.com/Voicemeeter/)**, installe-le en administrateur, puis redémarre. Remets ton casque et ton micro par défaut.
3. Ouvre Voicemeeter. Sur la colonne **VOICEMEETER VAIO** (l'entrée virtuelle), **allume B** (B1) et **éteins A** (A1). Ta voix part ainsi seulement vers la sortie virtuelle, pas dans ton casque.
4. Laisse **Voicemeeter ouvert**. Son menu permet de le lancer avec Windows et de le réduire près de l'horloge.
5. Dans SoundZ : onglet **Voix** → **Réglages** → **Câble vers les applis** : choisis **Voicemeeter Input**.
6. Dans Discord : périphérique d'entrée sur **Default**, ou **Voicemeeter Out B1**.

## Solution 4 — VB-CABLE + VB-CABLE A+B (don)

La plus simple pour tout avoir : SoundZ la reconnaît directement (musique sur CABLE Input, voix sur CABLE-A).

1. Installe **VB-CABLE** (solution 1).
2. Sur **[vb-audio.com/Cable](https://vb-audio.com/Cable/)**, fais un don pour **VB-CABLE A+B**. Tu reçois un lien de téléchargement personnel.
3. Lance **`VBCable_AB_PackSetup.exe`** en administrateur, redémarre, puis remets ton casque et ton micro par défaut.

Sans VB-CABLE, A+B suffit aussi : CABLE-A sert à la voix et CABLE-B à la musique.

## Solution 5 — Virtual Audio Cable (payant)

[Virtual Audio Cable](https://vac.muzychenko.net/) (VAC) est un logiciel payant d'un autre éditeur ; sa version d'essai ajoute une voix « trial » dans le son. SoundZ ne le reconnaît pas tout seul, il faut choisir ses câbles à la main :
- **Musique** : onglet Musique → Réglages → Sas : **Line 1** ;
- **Voix** : onglet Voix → Réglages → Câble vers les applis : **Line 2** ;
- **Discord** : périphérique d'entrée **Line 2**.

## Déconseillé

- **Pilotes « virtual audio » open source non signés.** Ils demandent d'activer le mode « test signing » de Windows, ce qui désactive une protection du système. Les anti-triche de nombreux jeux refusent alors de se lancer.
- **« Mixage stéréo » de la carte son** à la place d'un câble : ce qui sort de la carte son repart dans le micro. Ça crée de l'écho, la qualité est moindre, et seules certaines cartes son le proposent.

## Désinstaller un câble

- **VB-CABLE et A+B** : relance leur installateur en administrateur, puis **Remove Driver**, et redémarre.
- **Hi-Fi Cable et Voicemeeter** : *Paramètres → Applications*, ou leur installateur. Redémarre ensuite.

Pense à remettre ton casque et ton micro par défaut après coup.

---

VB-CABLE, VB-CABLE A+B, Hi-Fi Cable et Voicemeeter sont des logiciels de VB-Audio Software, et Virtual Audio Cable est un logiciel d'Eugene Muzychenko. Ils ne sont pas fournis avec SoundZ, et chacun a ses propres conditions d'utilisation.
