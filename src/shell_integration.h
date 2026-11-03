// Intégration à Windows, toujours au choix de l'utilisateur (menu ⚙ de SoundZ) :
// raccourci dans le menu Démarrer, icône sur le Bureau, lancement avec Windows.
// Rien d'autre n'est installé : SoundZ reste un dossier qu'on peut supprimer.
#pragma once

#include <string>

namespace shellint {

enum class Item { kStartMenu, kDesktop, kAutostart };

bool enabled(Item item);
bool set(Item item, bool on);  // false si Windows a refusé

// Au lancement : si SoundZ a changé de dossier, les raccourcis et le lancement avec Windows qui existent déjà
// sont mis à jour vers le nouvel emplacement.
void refresh();

// Lancé par Windows à l'ouverture de session (argument ajouté au lancement automatique).
constexpr wchar_t kStartupArgument[] = L"--demarrage";

}  // namespace shellint
