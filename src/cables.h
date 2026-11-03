// Répartition des câbles audio virtuels entre la musique (le sas) et la voix (le micro virtuel des applis).
// SoundZ marche avec ce qu'il trouve :
//  • VB-CABLE + VB-CABLE A+B : musique sur CABLE Input, voix sur CABLE-A ;
//  • VB-CABLE + Hi-Fi Cable (tous deux gratuits) : voix sur CABLE Input, musique sur Hi-Fi Cable ;
//  • VB-CABLE A+B seul : voix sur CABLE-A, musique sur CABLE-B ;
//  • un seul câble : il sert à la voix (ou à la musique, au choix, page « Appareils ») ;
//  • Voicemeeter : possible pour la voix seulement (sa sortie virtuelle est réglée dans Voicemeeter).
#pragma once

#include <string>
#include <vector>

#include "util.h"

namespace cables {

inline bool startsWith(const std::wstring& name, const wchar_t* prefix) { return lower(name).rfind(lower(prefix), 0) == 0; }

// Plus petit = mieux. -1 : ne convient pas.
inline int voiceRank(const std::wstring& name) {
    const wchar_t* order[] = {L"CABLE-A Input", L"CABLE-C Input", L"CABLE Input", L"Hi-Fi Cable Input", L"CABLE-B Input",
                              L"CABLE-D Input", L"Voicemeeter Input", L"Voicemeeter AUX Input", L"Voicemeeter VAIO3 Input"};
    for (int i = 0; i < int(sizeof(order) / sizeof(order[0])); ++i)
        if (startsWith(name, order[i])) return i;
    return -1;
}

// Pour la musique, le câble ne sert que de « sas » (personne ne l'écoute) : n'importe quel câble VB-Audio convient.
// Pas Voicemeeter : il renverrait aussi le son d'origine vers le casque (on l'entendrait deux fois).
inline int musicRank(const std::wstring& name) {
    const wchar_t* order[] = {L"CABLE Input", L"Hi-Fi Cable Input", L"CABLE-B Input", L"CABLE-D Input", L"CABLE-C Input",
                              L"CABLE-A Input"};
    for (int i = 0; i < int(sizeof(order) / sizeof(order[0])); ++i)
        if (startsWith(name, order[i])) return i;
    return -1;
}

inline bool isCable(const std::wstring& name) { return voiceRank(name) >= 0 || musicRank(name) >= 0; }

struct Assignment {
    int music = -1, voice = -1;  // index dans la liste des sorties (-1 : aucun)
    int count = 0;               // nombre de câbles utilisables trouvés
    bool singleToMusic = false;  // un seul câble, donné à la musique
};

// names : noms des sorties ; musicChoice / voiceChoice : câble choisi dans les réglages (-1 : automatique) ;
// singleForMusic : avec un seul câble, il va à la musique plutôt qu'à la voix.
inline Assignment assign(const std::vector<std::wstring>& names, int musicChoice, int voiceChoice, bool singleForMusic) {
    Assignment a;
    for (const auto& n : names) a.count += isCable(n) ? 1 : 0;
    auto best = [&](int (*rank)(const std::wstring&), int exclude) {
        int found = -1, bestRank = 1000;
        for (int i = 0; i < int(names.size()); ++i) {
            int r = rank(names[size_t(i)]);
            if (i != exclude && r >= 0 && r < bestRank) found = i, bestRank = r;
        }
        return found;
    };
    if (singleForMusic && a.count == 1 && musicChoice < 0 && voiceChoice < 0) {
        a.music = best(musicRank, -1);
        a.voice = best(voiceRank, a.music);
        a.singleToMusic = a.music >= 0;
        return a;
    }
    a.voice = voiceChoice >= 0 ? voiceChoice : best(voiceRank, musicChoice);
    a.music = musicChoice >= 0 ? musicChoice : best(musicRank, a.voice);
    if (a.music == a.voice) a.music = -1;  // jamais le même câble pour les deux
    return a;
}

}  // namespace cables
