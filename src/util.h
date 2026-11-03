#pragma once

#include <windows.h>

#include <cwctype>
#include <string>

inline std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
    return out;
}

inline std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring lower(std::wstring s) {
    for (auto& c : s) c = wchar_t(std::towlower(c));
    return s;
}

inline bool contains(const std::wstring& haystack, const std::wstring& needle) {
    return lower(haystack).find(lower(needle)) != std::wstring::npos;
}

inline bool sameText(const std::wstring& a, const std::wstring& b) {
    return lower(a) == lower(b);
}

// Appareil virtuel (câbles VB-Audio, Voicemeeter, entrées virtuelles…) : ni un vrai casque, ni un vrai micro.
// Aussi les entrées « tout ce que tu entends » des cartes son (What U Hear, Mixage stéréo) : pas des micros.
// Liste précise : beaucoup de vrais casques s'appellent « … Virtual Surround » ou « Virtual 7.1 ».
inline bool isVirtualDevice(const std::wstring& name) {
    const wchar_t* virtualNames[] = {L"CABLE",          L"VB-Audio",          L"Voicemeeter",   L"Virtual Audio Cable",
                                     L"Virtual Audio Driver", L"Virtual Mic Driver", L"Steam Streaming", L"MOTIV Mix Virtual",
                                     L"What U Hear",    L"Stereo Mix",        L"Mixage stéréo"};
    for (const wchar_t* v : virtualNames)
        if (contains(name, v)) return true;
    return false;
}

// « Haut-parleurs (Sound Blaster G8 USB-1) » -> « Sound Blaster G8 USB-1 » : le nom du matériel, plus parlant.
// Le numéro que Windows ajoute parfois devant (« 4- Shure MV7+ ») est retiré.
inline std::wstring shortDeviceName(const std::wstring& name) {
    std::wstring s = name;
    size_t open = name.find(L'('), close = name.rfind(L')');
    if (open != std::wstring::npos && close != std::wstring::npos && close > open + 1) s = name.substr(open + 1, close - open - 1);
    size_t i = 0;
    while (i < s.size() && iswdigit(s[i])) ++i;
    if (i > 0 && i + 1 < s.size() && s[i] == L'-' && s[i + 1] == L' ') s = s.substr(i + 2);
    return s;
}
