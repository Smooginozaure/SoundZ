// Soundboard : recherche de sons sur Myinstants (www.myinstants.com) et téléchargement.
// Sans IA : une simple requête au site, la lecture de sa page de résultats, puis le décodage du MP3.
// Tout se passe en arrière-plan ; la fenêtre relève les réponses avec take() (toutes les 50 ms).
#pragma once

#include <string>
#include <vector>

namespace myinstants {

constexpr int kMaxSeconds = 30;  // un son importé est coupé au-delà

struct Item {
    std::wstring title;  // « Ouais c'est greg »
    std::string mp3;     // « /media/sounds/ouais-cest-greg.mp3 »
};

struct Reply {
    int id = 0;
    bool isSearch = false;
    bool ok = false;
    std::wstring error;
    std::vector<Item> items;  // recherche
    bool more = false;        // le site a sans doute une page suivante
    std::string mp3;          // son
    std::vector<float> raw, cleaned;  // 48 kHz, mono : tel quel, et nettoyé (silences, volume)
    bool truncated = false;           // plus long que kMaxSeconds
};

void search(const std::wstring& query, int page, int id);  // page : 1, 2, 3…
void fetch(const std::string& mp3, int id);
bool take(Reply& out);
bool validSoundPath(const std::string& mp3);

}  // namespace myinstants
