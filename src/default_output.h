// Réglages Windows des sorties audio : sortie par défaut, effets (« améliorations audio »), volume.
#pragma once

#include <string>

struct IAudioEndpointVolume;

// Identifiant de la sortie par défaut actuelle (rôle « Multimédia »), vide en cas d'échec.
std::wstring getDefaultOutput();

// Définit la sortie par défaut pour la musique, les jeux et les vidéos.
// La sortie « Communications » (appels Discord/Teams…) n'est pas touchée.
bool setDefaultOutput(const std::wstring& endpointId);

// Améliorations audio (effets Nahimic & co.) d'une sortie :
// renvoie -1 si jamais réglé (effets actifs), 0 = effets actifs, 1 = effets désactivés.
int getEffectsDisabled(const std::wstring& endpointId);
bool setEffectsDisabled(const std::wstring& endpointId, bool disabled);

// Volume Windows d'une sortie.
class EndpointVolume {
public:
    ~EndpointVolume() { close(); }
    bool open(const std::wstring& endpointId);
    void close();
    bool isOpen() const { return volume_ != nullptr; }

    bool get(float& scalar, bool& muted);       // scalar : position du curseur Windows, 0..1
    bool set(float scalar, bool muted);
    float gain();                               // gain linéaire correspondant (0 si muet)

private:
    IAudioEndpointVolume* volume_ = nullptr;
};

// Appareil par défaut pour un rôle Windows (0 = console, 1 = multimédia, 2 = communications).
// capture = true : micros ; false : sorties.
std::wstring getDefaultDevice(bool capture, int role);
bool setDefaultDevice(const std::wstring& endpointId, int role);

// Type d'appareil selon Windows (PKEY_AudioEndpoint_FormFactor) : true pour un vrai casque, des enceintes,
// un micro-casque ou un micro ; false pour une entrée/sortie ligne, un écran, du S/PDIF… (ou inconnu).
bool isPersonalDevice(const std::wstring& endpointId);
