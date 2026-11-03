#include "shell_integration.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "util.h"

namespace shellint {
namespace {

// Ancienne façon de lancer SoundZ avec Windows (clé « Run » du registre), remplacée par un raccourci dans le
// dossier Démarrage : plus visible pour l'utilisateur, et que les antivirus ne confondent pas avec un logiciel
// malveillant qui s'installe en douce.
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"SoundZ";

std::wstring exePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring folderOf(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/")); }

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

std::wstring shortcutPath(Item item) {
    REFKNOWNFOLDERID id = item == Item::kStartMenu ? FOLDERID_Programs : item == Item::kDesktop ? FOLDERID_Desktop : FOLDERID_Startup;
    std::wstring folder = knownFolder(id);
    return folder.empty() ? L"" : folder + L"\\SoundZ.lnk";
}

bool exists(const std::wstring& path) { return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Cible d'un raccourci existant (vide si illisible).
std::wstring shortcutTarget(const std::wstring& path) {
    std::wstring target;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            wchar_t buffer[MAX_PATH] = {};
            if (SUCCEEDED(file->Load(path.c_str(), STGM_READ)) && SUCCEEDED(link->GetPath(buffer, MAX_PATH, nullptr, 0)))
                target = buffer;
            file->Release();
        }
        link->Release();
    }
    return target;
}

bool writeShortcut(const std::wstring& path, const wchar_t* arguments = nullptr) {
    const std::wstring exe = exePath();
    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        link->SetPath(exe.c_str());
        if (arguments) link->SetArguments(arguments);
        link->SetWorkingDirectory(folderOf(exe).c_str());
        link->SetDescription(L"SoundZ : musique transposée, voix (autotune, vocodeur) et soundboard");
        link->SetIconLocation(exe.c_str(), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            ok = SUCCEEDED(file->Save(path.c_str(), TRUE));
            file->Release();
        }
        link->Release();
    }
    if (ok) SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, path.c_str(), nullptr);
    return ok;
}

bool removeShortcut(const std::wstring& path) {
    if (!exists(path)) return true;
    bool ok = DeleteFileW(path.c_str()) != 0;
    if (ok) SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW, path.c_str(), nullptr);
    return ok;
}

std::wstring readLegacyAutostart() {
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return L"";
    return buffer;
}

void removeLegacyAutostart() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
}

// « "C:\…\SoundZ.exe" --demarrage » -> « C:\…\SoundZ.exe ».
std::wstring commandExe(const std::wstring& command) {
    if (!command.empty() && command[0] == L'"') {
        size_t end = command.find(L'"', 1);
        return end == std::wstring::npos ? command.substr(1) : command.substr(1, end - 1);
    }
    return command.substr(0, command.find(L' '));
}

}  // namespace

std::wstring shortcutFile(Item item) { return shortcutPath(item); }

bool enabled(Item item) {
    if (item == Item::kAutostart) return exists(shortcutPath(item)) || !readLegacyAutostart().empty();
    return exists(shortcutPath(item));
}

bool set(Item item, bool on) {
    const std::wstring path = shortcutPath(item);
    if (path.empty()) return false;
    if (item == Item::kAutostart) removeLegacyAutostart();
    if (on) return writeShortcut(path, item == Item::kAutostart ? kStartupArgument : nullptr);
    return removeShortcut(path);
}

// Au lancement. On ne touche à un raccourci que s'il vise un SoundZ.exe qui n'existe plus (le dossier de SoundZ a
// été déplacé). S'il vise un autre SoundZ.exe qui existe encore (une autre copie), on n'y touche pas : une copie
// lancée d'ailleurs, même pour un essai, ne doit jamais détourner les raccourcis de la vraie.
void refresh() {
    const std::wstring exe = exePath();
    for (Item item : {Item::kStartMenu, Item::kDesktop, Item::kAutostart}) {
        const std::wstring path = shortcutPath(item);
        if (!exists(path)) continue;
        std::wstring target = shortcutTarget(path);
        if (!target.empty() && !sameText(target, exe) && contains(lower(target), L"soundz.exe") && !exists(target))
            writeShortcut(path, item == Item::kAutostart ? kStartupArgument : nullptr);
    }
    // Ancien lancement automatique (registre) : remplacé par le raccourci du dossier Démarrage, s'il visait cet
    // exe-ci ou un exe disparu.
    std::wstring legacy = readLegacyAutostart();
    if (!legacy.empty()) {
        std::wstring target = commandExe(legacy);
        if (sameText(target, exe) || !exists(target)) {
            if (writeShortcut(shortcutPath(Item::kAutostart), kStartupArgument)) removeLegacyAutostart();
        }
    }
}

}  // namespace shellint
