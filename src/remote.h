// Télécommande sur téléphone : SoundZ sert une petite page web sur le réseau local (la box). Le téléphone l'ouvre
// en scannant un QR code, et y retrouve trois onglets : Musique (transposition), Voix (effets) et Soundboard.
// Rien ne passe par Internet ; un code secret, dans le QR code, empêche les autres appareils du Wi-Fi d'y accéder.
// Désactivée par défaut (⚙ → Télécommande sur téléphone).
#pragma once

#include <windows.h>

#include <string>

namespace remote {

constexpr UINT WM_APP_REMOTE = WM_APP + 7;  // une requête du téléphone, traitée par la fenêtre principale
constexpr int kBackId = 112;                // bouton « ← Retour » : même commande que celui de la page « Appareils »

// La page « Télécommande » de la fenêtre (ouverte depuis ⚙).
void create();
int layout(int top, bool shown);
bool onCommand(WPARAM wp, LPARAM lp);
bool onDrawItem(const DRAWITEMSTRUCT* item);
bool isStatus(HWND h, bool& error);
void pageShown();  // la page s'ouvre : adresse du PC relue
void onTimer();    // toutes les 500 ms : téléphones reliés

void startup();   // relance le serveur s'il était activé
void shutdown();  // à la fermeture

// Appelé par la fenêtre principale pour WM_APP_REMOTE (dans son fil d'exécution, comme un clic).
LRESULT onMessage(LPARAM request);

}  // namespace remote

// Onglet Musique (main.cpp), pour la télécommande.
namespace musicpage {
std::string remoteJson();
void remoteCommand(const std::string& action, int a, int b);  // semi (−4…+4), toggle
}  // namespace musicpage
