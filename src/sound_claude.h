// Soundboard : propose une image pour un son grâce à Claude Code (le programme « claude » installé sur le PC,
// avec l'abonnement de l'utilisateur). Seul le nom du son est envoyé. Claude répond par un emoji, ou par un
// dessin vectoriel (SVG). Tout se passe en arrière-plan : l'interface ne se bloque jamais.
#pragma once

#include <string>

namespace claude {

enum class Status { kChecking, kReady, kMissing, kLoggedOut };
enum class Kind { kEmoji, kDrawing };

void check();  // vérifie (en arrière-plan) que Claude Code est installé et connecté
Status status();

bool busy();  // une demande est en cours
// Lance une demande ; false si Claude n'est pas prêt ou déjà occupé.
bool request(Kind kind, const std::wstring& soundName);

// Résultat d'une demande terminée (une seule fois) : true s'il y en a un.
struct Result {
    Kind kind = Kind::kEmoji;
    bool ok = false;
    std::wstring emoji;  // pour un emoji
    std::string svg;     // pour un dessin (UTF-8)
    std::wstring error;
};
bool takeResult(Result& out);

void shutdown();  // arrête une demande en cours (à la fermeture)

}  // namespace claude
