// Overlay en jeu : un petit panneau posé par-dessus le jeu (ou n'importe quelle application), ouvert et fermé
// par un raccourci global. Il reste affiché jusqu'au prochain appui sur le raccourci et se commande à la souris :
// jouer un son du soundboard, découper une capture des 10 dernières secondes et la ranger.
//
// Le panneau ne prend jamais la main : le jeu reste au premier plan et garde le clavier. Aucune injection dans
// le jeu, rien n'est lu dans le jeu : c'est une fenêtre ordinaire de SoundZ, toujours au-dessus des autres.
#pragma once

#include <string>

namespace overlay {

void toggle();
void hide();
bool visible();

// Petite confirmation (« 10 dernières secondes gardées ») : dans l'overlay s'il est affiché, sinon en haut de
// l'écran quand SoundZ n'est pas au premier plan (en jeu, par exemple).
void notify(const std::wstring& text, bool error);

void onTimer();  // toutes les 50 ms, depuis la fenêtre principale
void shutdown();

}  // namespace overlay
