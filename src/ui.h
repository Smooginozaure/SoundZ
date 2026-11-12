// Outils communs de l'interface : échelle DPI, polices, création de contrôles, fichier SoundZ.ini.
#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>

#include "theme.h"

struct Ui {
    HWND wnd = nullptr;
    UINT dpi = 96;
    HFONT font = nullptr;       // texte normal
    HFONT bigFont = nullptr;    // grands titres
    HFONT smallFont = nullptr;  // petits textes
    std::wstring iniPath;
    // Largeur des pages (DIP) : la fenêtre s'élargit au glissé ; les pages s'étirent avec elle.
    int width = 400;
    // La sortie où l'utilisateur écoute (son casque, ses enceintes) : la sortie par défaut de Windows au
    // lancement. Sert à la musique transposée, au retour de la voix et au soundboard.
    std::wstring listenOutput, listenName;
};

extern Ui ui;

inline int S(int dip) { return MulDiv(dip, int(ui.dpi), 96); }

inline HWND makeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
    return CreateWindowExW(0, cls, text, WS_CHILD | style, 0, 0, 10, 10, ui.wnd, reinterpret_cast<HMENU>(INT_PTR(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

inline void place(HWND h, int x, int y, int w, int height, HFONT font) {
    MoveWindow(h, S(x), S(y), S(w), S(height), TRUE);
    SendMessageW(h, WM_SETFONT, WPARAM(font), TRUE);
}

inline void setText(HWND h, const std::wstring& text) {
    wchar_t current[512] = {};
    GetWindowTextW(h, current, 512);
    if (text != current) SetWindowTextW(h, text.c_str());
}

inline std::wstring iniRead(const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(section, key, fallback, buffer, 512, ui.iniPath.c_str());
    return buffer;
}

inline int iniReadInt(const wchar_t* section, const wchar_t* key, int fallback) {
    std::wstring v = iniRead(section, key);
    return v.empty() ? fallback : _wtoi(v.c_str());
}

inline void iniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), ui.iniPath.c_str());
}

inline void iniDeleteSection(const wchar_t* section) {
    WritePrivateProfileStringW(section, nullptr, nullptr, ui.iniPath.c_str());
    // Plus rien dedans : pas de fichier qui traîne.
    wchar_t names[64] = {};
    if (GetPrivateProfileSectionNamesW(names, 64, ui.iniPath.c_str()) == 0) DeleteFileW(ui.iniPath.c_str());
}

// Curseur Windows (trackbar) avec une plage donnée.
inline HWND makeSlider(int minValue, int maxValue, DWORD extraStyle = TBS_NOTICKS, int id = 0) {
    HWND h = makeControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_BOTTOM | extraStyle, id);
    SendMessageW(h, TBM_SETRANGEMIN, FALSE, minValue);
    SendMessageW(h, TBM_SETRANGEMAX, TRUE, maxValue);
    SendMessageW(h, TBM_SETLINESIZE, 0, 1);
    SendMessageW(h, TBM_SETPAGESIZE, 0, 1);
    if (extraStyle & TBS_AUTOTICKS) SendMessageW(h, TBM_SETTICFREQ, 1, 0);
    theme::slider(h);  // dessiné par le thème (fin, avec un bouton rond)
    return h;
}

inline int sliderPos(HWND h) { return int(SendMessageW(h, TBM_GETPOS, 0, 0)); }
inline void setSliderPos(HWND h, int v) { SendMessageW(h, TBM_SETPOS, TRUE, v); }
