// Onglet « Voix » : autotune / vocodeur sur le micro, envoyé aux applis par CABLE-A.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "engine.h"  // OutputDevice

namespace voicepage {

constexpr UINT WM_APP_VOICE_LOST = WM_APP + 2;
constexpr UINT WM_APP_RELAYOUT = WM_APP + 3;  // la page demande à la fenêtre de se réorganiser
constexpr UINT WM_APP_CABLES_CHANGED = WM_APP + 5;  // le câble de la voix a changé : la musique revoit le sien

void create();                    // crée les contrôles (cachés)
int layout(int top, bool shown);  // place les contrôles ; renvoie le bas de la page (en DIP)
bool onCommand(WPARAM wp, LPARAM lp);
bool onScroll(HWND slider);
bool isStatus(HWND h, bool& error);  // pour la couleur du texte d'état
void onTimer();                   // affichage de la note et du niveau
void onDeviceLost();

void startup();   // au lancement : réparation après plantage, puis remise en marche si c'était actif
// Page « Appareils » (menu ⚙) : la liste des micros, celui utilisé, et la prise en compte d'un nouveau choix.
std::vector<OutputDevice> microphones();
std::wstring currentMicName();
void reloadDevices();  // relit les choix (micro, retour casque) et relance la voix si elle tourne

std::wstring deviceReport();  // --devicetest : micro, câble et retour casque que SoundZ choisirait (sans rien ouvrir)
bool running();   // la voix part dans le câble (les sons du soundboard aussi)
void shutdown();  // à la fermeture : arrêt propre et remise en place du micro par défaut

}  // namespace voicepage
