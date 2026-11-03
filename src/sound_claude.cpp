#include "sound_claude.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#include "util.h"

namespace claude {
namespace {

std::atomic<Status> g_status{Status::kChecking};
std::thread g_checker, g_worker;
std::atomic<bool> g_busy{false}, g_cancel{false};
std::mutex g_mutex;
bool g_hasResult = false;
Result g_result;

constexpr DWORD kTimeoutMs = 120000;

const wchar_t kSystem[] =
    L"Tu fournis des images pour les boutons d'un soundboard. Tu réponds uniquement avec ce qui est demandé, "
    L"sans aucun autre texte.";

std::wstring findExe() {
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"claude.exe", nullptr, MAX_PATH, found, nullptr)) return found;
    wchar_t profile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
        std::wstring local = std::wstring(profile) + L"\\.local\\bin\\claude.exe";
        if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return local;
    }
    return L"";
}

std::string utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), out.data(), n);
    return out;
}

// Lance « claude » sans fenêtre, lui donne input sur son entrée, et renvoie ce qu'il écrit.
bool run(const std::wstring& args, const std::string& input, std::string& output) {
    std::wstring exe = findExe();
    if (exe.empty()) return false;
    // Dossier de travail vide : Claude Code ne voit aucun projet.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"soundz-claude";
    CreateDirectoryW(dir.c_str(), nullptr);

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0)) return false;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
        CloseHandle(inRead);
        CloseHandle(inWrite);
        return false;
    }
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = nul;
    PROCESS_INFORMATION pi = {};
    std::wstring command = L"\"" + exe + L"\" " + args;
    BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(),
                                  &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return false;
    }
    DWORD written = 0;
    if (!input.empty()) WriteFile(inWrite, input.data(), DWORD(input.size()), &written, nullptr);
    CloseHandle(inWrite);

    const DWORD start = GetTickCount();
    output.clear();
    bool finished = false;
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(outRead, nullptr, 0, nullptr, &available, nullptr)) {
            finished = true;  // tuyau fermé : le programme a fini
            break;
        }
        if (available > 0) {
            char buffer[4096];
            DWORD got = 0;
            if (ReadFile(outRead, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr) && got > 0)
                output.append(buffer, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            // Dernier morceau éventuel.
            while (PeekNamedPipe(outRead, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
                char buffer[4096];
                DWORD got = 0;
                if (!ReadFile(outRead, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr) || got == 0) break;
                output.append(buffer, got);
            }
            finished = true;
            break;
        }
        if (g_cancel || GetTickCount() - start > kTimeoutMs) break;
    }
    if (!finished) TerminateProcess(pi.hProcess, 1);
    DWORD code = 1;
    WaitForSingleObject(pi.hProcess, 2000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(outRead);
    return finished && code == 0;
}

std::wstring quote(const std::wstring& s) {  // argument entre guillemets (le texte n'en contient pas)
    return L"\"" + s + L"\"";
}

std::wstring askArgs(const wchar_t* model) {
    return std::wstring(L"-p --safe-mode --tools \"\" --no-session-persistence --model ") + model + L" --system-prompt " +
           quote(kSystem);
}

std::string emojiPrompt(const std::wstring& name) {
    return utf8(L"Choisis l'emoji Unicode qui illustre le mieux un bouton de soundboard intitulé « " + name +
                L" ». Réponds uniquement par cet emoji (deux au maximum), sans aucun autre texte.");
}

std::string drawingPrompt(const std::wstring& name) {
    return utf8(
        L"Dessine une icône pour un bouton de soundboard intitulé : « " + name +
        L" ».\n"
        L"Réponds uniquement avec un document SVG complet : pas de texte autour, pas de bloc de code.\n"
        L"Contraintes :\n"
        L"- racine : <svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">, sans width ni height ;\n"
        L"- style autocollant : couleurs vives, formes simples et expressives, contour sombre épais, fond transparent, "
        L"le sujet remplit le carré ;\n"
        L"- éléments permis : svg, g, defs, path, circle, ellipse, rect, polygon, polyline, line, linearGradient, "
        L"radialGradient, stop ;\n"
        L"- couleurs et traits en attributs (fill, stroke, stroke-width, stroke-linecap, stroke-linejoin, opacity, "
        L"fill-opacity, transform) ;\n"
        L"- interdits : text, style, CSS, class, filter, mask, clipPath, image, use, script, polices ;\n"
        L"- moins de 6000 caractères.");
}

// Garde l'emoji de la réponse (première ligne, sans espaces ; pas de lettres ni de phrase).
std::wstring pickEmoji(const std::string& output) {
    std::wstring text = fromUtf8(output);
    std::wstring line;
    for (wchar_t c : text) {
        if (c == L'\n' || c == L'\r') {
            if (!line.empty()) break;
            continue;
        }
        if (c != L' ' && c != L'\t') line += c;
    }
    if (line.empty() || line.size() > 16) return L"";
    for (wchar_t c : line)
        if (c < 0x80) return L"";  // une phrase, pas un emoji
    return line;
}

void worker(Kind kind, std::wstring name) {
    Result r;
    r.kind = kind;
    std::string output;
    bool ran = run(askArgs(kind == Kind::kEmoji ? L"haiku" : L"sonnet"),
                   kind == Kind::kEmoji ? emojiPrompt(name) : drawingPrompt(name), output);
    if (!ran) {
        r.error = g_cancel ? L"" : L"Claude n'a pas répondu (pas de connexion internet, ou quota atteint ?). Réessaie.";
    } else if (kind == Kind::kEmoji) {
        r.emoji = pickEmoji(output);
        r.ok = !r.emoji.empty();
        if (!r.ok) r.error = L"Claude n'a pas proposé d'emoji. Réessaie.";
    } else {
        size_t from = output.find("<svg"), to = output.rfind("</svg>");
        r.ok = from != std::string::npos && to != std::string::npos && to > from;
        if (r.ok) r.svg = output.substr(from, to + 6 - from);
        else r.error = L"Claude n'a pas renvoyé de dessin. Réessaie.";
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_result = r;
        g_hasResult = true;
    }
    g_busy = false;
}

}  // namespace

void check() {
    if (g_checker.joinable()) {
        if (g_status == Status::kChecking) return;  // déjà en cours
        g_checker.join();
    }
    g_status = Status::kChecking;
    g_checker = std::thread([] {
        if (findExe().empty()) {
            g_status = Status::kMissing;
            return;
        }
        std::string output;
        run(L"auth status", "", output);
        // Réponse JSON : « "loggedIn": true ».
        size_t at = output.find("\"loggedIn\"");
        bool loggedIn = at != std::string::npos && output.find("true", at) != std::string::npos &&
                        output.find("true", at) < output.find_first_of(",}", at);
        g_status = loggedIn ? Status::kReady : Status::kLoggedOut;
    });
}

Status status() { return g_status; }

bool busy() { return g_busy; }

bool request(Kind kind, const std::wstring& soundName) {
    if (g_status != Status::kReady || g_busy) return false;
    if (g_worker.joinable()) g_worker.join();
    // Le nom passe dans le texte de la demande : on retire les retours à la ligne.
    std::wstring name;
    for (wchar_t c : soundName) name += (c == L'\r' || c == L'\n') ? L' ' : c;
    g_busy = true;
    g_cancel = false;
    g_worker = std::thread(worker, kind, name);
    return true;
}

bool takeResult(Result& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_hasResult) return false;
    out = g_result;
    g_hasResult = false;
    return true;
}

void shutdown() {
    g_cancel = true;
    if (g_worker.joinable()) g_worker.join();
    if (g_checker.joinable()) g_checker.join();
}

}  // namespace claude
