#include "shell_integration.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "util.h"

namespace shellint {
namespace {

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
    std::wstring folder = knownFolder(item == Item::kStartMenu ? FOLDERID_Programs : FOLDERID_Desktop);
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

bool writeShortcut(const std::wstring& path) {
    const std::wstring exe = exePath();
    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        link->SetPath(exe.c_str());
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

std::wstring autostartCommand() { return L"\"" + exePath() + L"\" " + kStartupArgument; }

std::wstring readAutostart() {
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return L"";
    return buffer;
}

bool writeAutostart(bool on) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS status;
    if (on) {
        std::wstring command = autostartCommand();
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

}  // namespace

bool enabled(Item item) {
    if (item == Item::kAutostart) return !readAutostart().empty();
    return exists(shortcutPath(item));
}

bool set(Item item, bool on) {
    if (item == Item::kAutostart) return writeAutostart(on);
    const std::wstring path = shortcutPath(item);
    if (path.empty()) return false;
    if (on) return writeShortcut(path);
    if (!exists(path)) return true;
    bool ok = DeleteFileW(path.c_str()) != 0;
    if (ok) SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW, path.c_str(), nullptr);
    return ok;
}

void refresh() {
    const std::wstring exe = exePath();
    for (Item item : {Item::kStartMenu, Item::kDesktop}) {
        const std::wstring path = shortcutPath(item);
        if (!exists(path)) continue;
        // Seulement nos raccourcis : ceux qui visent un SoundZ.exe (ailleurs : l'ancien emplacement).
        std::wstring target = shortcutTarget(path);
        if (!target.empty() && !sameText(target, exe) && contains(lower(target), L"soundz.exe")) writeShortcut(path);
    }
    std::wstring command = readAutostart();
    if (!command.empty() && !sameText(command, autostartCommand())) writeAutostart(true);
}

}  // namespace shellint
