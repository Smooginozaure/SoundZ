#include "myinstants.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <thread>

#include "miniaudio/miniaudio.h"
#include "sound_dsp.h"
#include "util.h"

namespace myinstants {
namespace {

constexpr wchar_t kHost[] = L"www.myinstants.com";
constexpr size_t kMaxPage = 4 << 20, kMaxSound = 12 << 20;

// État partagé avec les fils de téléchargement. Jamais libéré : un fil encore en route à la fermeture
// de SoundZ ne touche ainsi jamais de la mémoire rendue.
struct Shared {
    std::mutex mutex;
    std::deque<Reply> replies;
};
Shared& shared() {
    static Shared* s = new Shared;
    return *s;
}

void post(Reply&& reply) {
    std::lock_guard<std::mutex> lock(shared().mutex);
    shared().replies.push_back(std::move(reply));
}

bool httpGet(const std::wstring& path, size_t limit, std::string& body, std::wstring& error) {
    body.clear();
    HINTERNET session = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) SoundZ/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)  // Windows 8 et plus anciens : pas de proxy automatique
        session = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) SoundZ/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = session ? WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                   : nullptr;
    bool ok = false;
    if (request) {
        WinHttpSetTimeouts(request, 8000, 8000, 10000, 20000);
        DWORD status = 0, size = sizeof(status);
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr) &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                &size, WINHTTP_NO_HEADER_INDEX)) {
            if (status != 200) {
                error = L"Myinstants a répondu « erreur " + std::to_wstring(status) + L" ». Réessaie dans un moment.";
            } else {
                ok = true;
                for (;;) {
                    DWORD available = 0, read = 0;
                    if (!WinHttpQueryDataAvailable(request, &available)) {
                        ok = false;
                        break;
                    }
                    if (available == 0) break;
                    if (body.size() + available > limit) {
                        ok = false;
                        error = L"Le fichier de Myinstants est trop gros.";
                        break;
                    }
                    const size_t at = body.size();
                    body.resize(at + available);
                    if (!WinHttpReadData(request, &body[at], available, &read)) {
                        ok = false;
                        break;
                    }
                    body.resize(at + read);
                }
                if (!ok && error.empty()) error = L"La connexion avec Myinstants a été coupée. Réessaie.";
            }
        }
    }
    if (!ok && error.empty()) error = L"Impossible de joindre Myinstants : vérifie ta connexion à Internet.";
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

std::wstring encodeQuery(const std::wstring& text) {
    static const wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : narrow(text)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += wchar_t(c);
        } else if (c == ' ') {
            out += L'+';
        } else {
            out += L'%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// « C&#x27;est vrai ? » -> « C'est vrai ? »
std::wstring decodeEntities(const std::string& html) {
    std::string out;
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] != '&') {
            out += html[i];
            continue;
        }
        const size_t end = html.find(';', i);
        if (end == std::string::npos || end - i > 10) {
            out += '&';
            continue;
        }
        const std::string name = html.substr(i + 1, end - i - 1);
        unsigned long code = 0;
        if (name == "amp") code = '&';
        else if (name == "lt") code = '<';
        else if (name == "gt") code = '>';
        else if (name == "quot") code = '"';
        else if (name == "apos") code = '\'';
        else if (name == "nbsp") code = ' ';
        else if (name.size() > 1 && name[0] == '#')
            code = (name[1] == 'x' || name[1] == 'X') ? strtoul(name.c_str() + 2, nullptr, 16) : strtoul(name.c_str() + 1, nullptr, 10);
        if (code == 0 || code > 0x10FFFF) {
            out += '&';
            continue;
        }
        // Le caractère, en UTF-8.
        if (code < 0x80) {
            out += char(code);
        } else if (code < 0x800) {
            out += char(0xC0 | (code >> 6));
            out += char(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += char(0xE0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        } else {
            out += char(0xF0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3F));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        }
        i = end;
    }
    std::wstring text = widen(out);
    // Espaces en trop (retours à la ligne de la page).
    std::wstring clean;
    for (wchar_t c : text) {
        if (iswspace(c)) {
            if (!clean.empty() && clean.back() != L' ') clean += L' ';
        } else {
            clean += c;
        }
    }
    while (!clean.empty() && clean.back() == L' ') clean.pop_back();
    return clean;
}

// Chaque son de la page : un bouton « play('/media/sounds/….mp3', …) », puis son titre (lien « instant-link »).
std::vector<Item> parse(const std::string& html) {
    std::vector<Item> items;
    const std::string play = "onclick=\"play('", link = "class=\"instant-link";
    size_t at = 0;
    while ((at = html.find(play, at)) != std::string::npos) {
        at += play.size();
        const size_t end = html.find('\'', at);
        if (end == std::string::npos) break;
        const std::string mp3 = html.substr(at, end - at);
        const size_t nextPlay = html.find(play, end);
        size_t title = html.find(link, end);
        if (title == std::string::npos || (nextPlay != std::string::npos && title > nextPlay)) continue;
        title = html.find('>', title);
        const size_t close = title == std::string::npos ? title : html.find('<', title);
        if (close == std::string::npos) continue;
        Item item;
        item.mp3 = mp3;
        item.title = decodeEntities(html.substr(title + 1, close - title - 1));
        if (!validSoundPath(item.mp3) || item.title.empty()) continue;
        bool seen = false;
        for (const Item& other : items) seen = seen || other.mp3 == item.mp3;
        if (!seen) items.push_back(std::move(item));
    }
    return items;
}

// MP3 -> 48 kHz mono (miniaudio convertit et rééchantillonne).
bool decode(const std::string& bytes, std::vector<float>& out, bool& truncated) {
    out.clear();
    truncated = false;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, sound::kRate);
    ma_decoder decoder;
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS) return false;
    const size_t limit = size_t(kMaxSeconds) * sound::kRate;
    float chunk[4096];
    for (;;) {
        ma_uint64 read = 0;
        if (ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read) != MA_SUCCESS || read == 0) break;
        out.insert(out.end(), chunk, chunk + read);
        if (out.size() >= limit) {
            truncated = true;
            out.resize(limit);
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    return !out.empty();
}

}  // namespace

bool validSoundPath(const std::string& mp3) {
    if (mp3.rfind("/media/sounds/", 0) != 0 || mp3.size() < 19 || mp3.size() > 200) return false;
    if (mp3.find("..") != std::string::npos || mp3.compare(mp3.size() - 4, 4, ".mp3") != 0) return false;
    for (char c : mp3)
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '-' || c == '_' || c == '.' || c == '%')) return false;
    return true;
}

void search(const std::wstring& query, int page, int id) {
    std::thread([query, page, id] {
        Reply reply;
        reply.id = id;
        reply.isSearch = true;
        std::string html;
        std::wstring path = L"/fr/search/?name=" + encodeQuery(query) + (page > 1 ? L"&page=" + std::to_wstring(page) : L"");
        if (httpGet(path, kMaxPage, html, reply.error)) {
            reply.items = parse(html);
            reply.ok = true;
            reply.more = reply.items.size() >= 30;  // page pleine (36 sons) : il y en a sans doute d'autres
        }
        post(std::move(reply));
    }).detach();
}

void fetch(const std::string& mp3, int id) {
    std::thread([mp3, id] {
        Reply reply;
        reply.id = id;
        reply.mp3 = mp3;
        std::string bytes;
        if (!validSoundPath(mp3)) {
            reply.error = L"Adresse de son invalide.";
        } else if (httpGet(widen(mp3), kMaxSound, bytes, reply.error)) {
            if (!decode(bytes, reply.raw, reply.truncated)) {
                reply.error = L"Ce son de Myinstants n'a pas pu être lu.";
            } else {
                sound::CleanResult cleaned = sound::clean(reply.raw, 0.0f);  // silences coupés, volume égalisé
                if (cleaned.empty) {
                    reply.error = L"Ce son de Myinstants est silencieux.";
                } else {
                    reply.cleaned = std::move(cleaned.samples);
                    reply.ok = true;
                }
            }
        }
        post(std::move(reply));
    }).detach();
}

bool take(Reply& out) {
    std::lock_guard<std::mutex> lock(shared().mutex);
    if (shared().replies.empty()) return false;
    out = std::move(shared().replies.front());
    shared().replies.pop_front();
    return true;
}

}  // namespace myinstants
