// Thème de SoundZ : sombre et sobre (dans l'esprit de Discord), teinte « Orage » (bleu-gris #3E5068).
// Les contrôles Windows sont dessinés à la main (boutons, onglets, interrupteurs, curseurs, jauges, cases de
// raccourci) ; la disposition, elle, ne change pas.
#pragma once

#include <windows.h>

#include <string>

namespace theme {

// ---- palette
constexpr COLORREF kBg = RGB(30, 31, 34);              // fond de la fenêtre
constexpr COLORREF kSurface = RGB(43, 45, 49);         // boutons du soundboard, zones
constexpr COLORREF kSurfaceHover = RGB(53, 55, 60);
constexpr COLORREF kSurfacePressed = RGB(63, 65, 71);
constexpr COLORREF kButton = RGB(56, 58, 64);          // boutons ordinaires
constexpr COLORREF kButtonHover = RGB(67, 69, 75);
constexpr COLORREF kInput = RGB(17, 18, 20);           // champs de saisie
constexpr COLORREF kBorder = RGB(63, 65, 71);
constexpr COLORREF kText = RGB(219, 222, 225);
constexpr COLORREF kText2 = RGB(181, 186, 193);        // libellés
constexpr COLORREF kMuted = RGB(148, 155, 164);        // textes d'état
constexpr COLORREF kFaint = RGB(128, 132, 142);        // détails (raccourcis…)
constexpr COLORREF kDisabled = RGB(94, 98, 106);
constexpr COLORREF kAccent = RGB(62, 80, 104);         // Orage #3E5068 : titre, onglet actif, bouton principal
constexpr COLORREF kAccentHover = RGB(74, 94, 121);
constexpr COLORREF kAccentBright = RGB(96, 121, 153);  // curseurs, interrupteurs (plus lisible sur le fond sombre)
constexpr COLORREF kAccentLight = RGB(160, 180, 205);  // poignées, points
constexpr COLORREF kAccentSoft = RGB(50, 58, 70);      // bouton en cours de lecture, passage sélectionné
constexpr COLORREF kDanger = RGB(242, 63, 67);
constexpr COLORREF kAmber = RGB(227, 180, 95);
constexpr COLORREF kAmberLine = RGB(185, 138, 58);
constexpr COLORREF kAmberSoft = RGB(58, 52, 40);

enum class Kind {
    kNone,
    kButton,   // bouton ordinaire
    kPrimary,  // bouton principal (teinte Orage)
    kSegment,  // choix parmi plusieurs (coché = teinte Orage)
    kTab,      // onglet
    kToggle,   // interrupteur avec son libellé (remplace la case à cocher)
    kTile,     // dessiné par la page (soundboard) ; le thème ne gère que le survol
};

void init();  // avant de créer la fenêtre
void shutdown();
void decorateWindow(HWND top);  // barre de titre teintée, mode sombre de Windows

HBRUSH brush(COLORREF color);

// Contrôles dessinés par le thème.
HWND button(const wchar_t* text, Kind kind, int id = 0);
HWND hotkeyBox(int id = 0);  // case de raccourci (compatible HKM_SETHOTKEY / HKM_GETHOTKEY / HKM_SETRULES)
HWND meter(int id = 0);      // jauge (compatible PBM_SETRANGE32 / PBM_SETPOS)
void slider(HWND trackbar, bool bipolar = false);  // curseur ; bipolar : rempli depuis le 0 central
void darkControl(HWND control);                    // liste déroulante / champ : version sombre de Windows
// Champ de saisie posé dans un cadre arrondi dessiné par la fenêtre (coordonnées en DIP).
void placeInput(HWND edit, int x, int y, int width, int height, HFONT font);

bool isHover(HWND control);
bool isChecked(HWND control);

// À appeler depuis la fenêtre principale.
void onCommand(HWND control, int code);           // WM_COMMAND (avant les pages)
bool drawItem(const DRAWITEMSTRUCT* item);         // WM_DRAWITEM
bool customDraw(const NMHDR* header, LRESULT& result);  // WM_NOTIFY
LRESULT ctlColor(UINT message, HDC dc, HWND control, COLORREF text);  // WM_CTLCOLOR*
void paint(HDC dc);                                // WM_PAINT : cadres des champs

// Dessin (anticrénelé).
void roundRect(HDC dc, const RECT& r, float radius, COLORREF fill);
void roundFrame(HDC dc, const RECT& r, float radius, COLORREF color, float width, bool dashed = false);
void circle(HDC dc, float cx, float cy, float radius, COLORREF fill);

std::wstring hotkeyText(WORD hotkey);  // « Ctrl+Maj+1 »

}  // namespace theme
