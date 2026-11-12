// Onglet « Soundboard » : capture quelques secondes du son du PC, les nettoie, et les range sous des boutons
// (avec une image). Un clic, ou un raccourci clavier, envoie le son dans le micro virtuel (CABLE-A),
// donc dans Discord, WhatsApp…
#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class SoundMixer;
namespace soundimg {
struct Image;
}

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

// ---- Pour l'overlay en jeu (overlay.cpp).
using BoardSamples = std::shared_ptr<const std::vector<float>>;
struct BoardSound {
    int uid;
    int tag;  // bit dans playingMask()
    std::wstring name;
    const soundimg::Image* image;  // valable jusqu'au prochain changement de boardVersion()
    bool unnamed;                  // gardé en jeu, à nommer sur le bureau
    std::wstring hotkey;           // « Ctrl+Maj+1 », vide sans raccourci
};
struct BoardCapture {
    std::wstring file, time;  // capture « à trier » (10 dernières secondes)
};
uint32_t boardVersion();  // change à chaque modification des sons ou des captures
std::vector<BoardSound> boardSounds();
std::vector<BoardCapture> boardCaptures();
uint64_t playingMask();
void playUid(int uid);
void stopSounds();
bool soundsGoToMic();   // la voix tourne : les sons partent dans le micro
bool hearsInHeadset();  // « Entendre les sons du soundboard »
void preview(const BoardSamples& samples, float gain);  // dans le casque seulement
void stopPreview();
int defaultCleaning();
int defaultSeconds();
std::wstring overlayHotkeyText();
// Range une capture dans le soundboard (nom et image à choisir plus tard, sur le bureau).
// Renvoie un message d'erreur, vide si tout va bien.
std::wstring keepCapture(const std::wstring& file, const std::vector<float>& raw, size_t selStart, size_t selEnd, int volume,
                         int cleaning, const BoardSamples& cleaned);
void discardCapture(const std::wstring& file);

// ---- Pour la télécommande du téléphone (remote.cpp) : comme un clic sur le PC.
std::string remoteJson();
void remoteCommand(const std::string& action, int a, int b);  // play (uid), stop, replay, volume (0–100)
bool remoteImage(int uid, std::string& png);                   // image du bouton, en PNG

}  // namespace soundpage
