// Onglet « Soundboard » : capture quelques secondes du son du PC, les nettoie, et les range sous des boutons
// (avec une image). Un clic, ou un raccourci clavier, envoie le son dans le micro virtuel (CABLE-A),
// donc dans Discord, WhatsApp…
#pragma once

#include <windows.h>

class SoundMixer;

namespace soundpage {

void create();
int layout(int top, bool shown);
bool onCommand(WPARAM wp, LPARAM lp);
bool onScroll(HWND slider, int code);  // code : TB_THUMBTRACK, TB_ENDTRACK…
bool onDrawItem(const DRAWITEMSTRUCT* item);
bool onContextMenu(HWND control, int x, int y);
bool onHotkey(int id);
bool isStatus(HWND h, bool& error);
void onTimer();

void reloadDevices();  // la sortie d'écoute a changé (page « Appareils ») : le lecteur du casque suit

void startup();
void shutdown();

// Sons envoyés dans le micro virtuel : le moteur de la voix les mélange à la voix.
SoundMixer* cableMixer();

}  // namespace soundpage
