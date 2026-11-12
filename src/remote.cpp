// Télécommande sur téléphone : un petit serveur web sur le réseau local, et la page « Télécommande » de la fenêtre.
//
// Le serveur écoute dans ses propres fils d'exécution ; chaque requête est confiée à la fenêtre principale
// (WM_APP_REMOTE), qui la traite comme un clic : les pages de SoundZ ne sont jamais touchées depuis un autre fil.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <netlistmgr.h>

#include "remote.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "qrcodegen/qrcodegen.hpp"
#include "remote_web.h"
#include "sound_image.h"
#include "sound_page.h"
#include "ui.h"
#include "util.h"
#include "voice_page.h"

namespace remote {
namespace {

constexpr int kDefaultPort = 47800;
constexpr wchar_t kSection[] = L"telecommande";
constexpr size_t kMaxHeader = 8192, kMaxBody = 4096;
constexpr int kMaxClients = 12;
constexpr DWORD kPhoneSeenMs = 4000;  // un téléphone est « relié » s'il a demandé l'état il y a moins de 4 s

enum ControlId : int { IDC_REMOTE_ON = 800, IDC_REMOTE_NEWCODE, IDC_REMOTE_QR };

// ---------------------------------------------------------------- serveur (fils d'exécution du réseau)

struct Request {
    std::string method, path, query, body, peer;
    int status = 500;
    std::string type = "text/plain; charset=utf-8", content, cache = "no-store";
};

std::atomic<bool> g_stopping{false};
std::atomic<int> g_active{0};  // connexions en cours (chacune dans son fil)
SOCKET g_listener = INVALID_SOCKET;
std::thread g_acceptThread;
std::mutex g_clientsMutex;
std::vector<SOCKET> g_clients;
bool g_wsa = false;

bool sendAll(SOCKET s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(s, data.data() + sent, int(std::min<size_t>(data.size() - sent, 1 << 20)), 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

const char* statusText(int status) {
    switch (status) {
    case 200: return "OK";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    }
    return "Internal Server Error";
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// Une connexion : plusieurs requêtes à la suite (keep-alive), jusqu'à 15 s sans rien.
void serveClient(SOCKET s, const std::string& peer) {
    DWORD timeout = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    BOOL noDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    std::string buffer;
    char chunk[4096];
    auto receive = [&]() {
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
        buffer.append(chunk, size_t(n));
        return true;
    };
    while (!g_stopping) {
        size_t end;
        bool complete = true;
        while ((end = buffer.find("\r\n\r\n")) == std::string::npos) {
            if (buffer.size() > kMaxHeader || !receive()) {
                complete = false;
                break;
            }
        }
        if (!complete) break;
        const std::string head = buffer.substr(0, end);
        buffer.erase(0, end + 4);

        Request req;
        req.peer = peer;
        size_t lineEnd = head.find("\r\n");
        const std::string line = head.substr(0, lineEnd);
        size_t sp1 = line.find(' '), sp2 = line.rfind(' ');
        if (sp1 == std::string::npos || sp2 == sp1) break;
        req.method = line.substr(0, sp1);
        std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
        size_t q = target.find('?');
        req.path = target.substr(0, q);
        if (q != std::string::npos) req.query = target.substr(q + 1);
        size_t length = 0;
        bool close = false;
        for (size_t at = lineEnd; at != std::string::npos && at < head.size();) {
            size_t next = head.find("\r\n", at + 2);
            std::string header = lowerAscii(head.substr(at + 2, next == std::string::npos ? std::string::npos : next - at - 2));
            if (header.rfind("content-length:", 0) == 0) length = size_t(strtoul(header.c_str() + 15, nullptr, 10));
            if (header.rfind("connection:", 0) == 0 && header.find("close") != std::string::npos) close = true;
            at = next;
        }
        if (length > kMaxBody) {
            sendAll(s, "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            break;
        }
        while (buffer.size() < length && receive()) {}
        if (buffer.size() < length) break;
        req.body = buffer.substr(0, length);
        buffer.erase(0, length);

        if (req.method != "GET" && req.method != "POST") {
            req.status = 405;
        } else if (!g_stopping) {
            // Traité par la fenêtre principale, comme un clic (elle répond tout de suite si elle ferme).
            SendMessageW(ui.wnd, WM_APP_REMOTE, 0, reinterpret_cast<LPARAM>(&req));
        } else {
            req.status = 503;
        }
        std::string response = "HTTP/1.1 " + std::to_string(req.status) + " " + statusText(req.status) +
                               "\r\nContent-Type: " + req.type + "\r\nContent-Length: " + std::to_string(req.content.size()) +
                               "\r\nCache-Control: " + req.cache +
                               "\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: " +
                               (close ? "close" : "keep-alive") + "\r\n\r\n";
        if (!sendAll(s, response + req.content) || close) break;
    }
}

void acceptLoop() {
    while (!g_stopping) {
        sockaddr_in address = {};
        int size = sizeof(address);
        SOCKET client = accept(g_listener, reinterpret_cast<sockaddr*>(&address), &size);
        if (client == INVALID_SOCKET) {
            if (g_stopping) break;
            Sleep(50);
            continue;
        }
        if (g_active >= kMaxClients) {
            closesocket(client);
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
        {
            std::lock_guard<std::mutex> lock(g_clientsMutex);
            g_clients.push_back(client);
        }
        ++g_active;
        std::thread([client, peer = std::string(ip)] {
            serveClient(client, peer);
            {
                std::lock_guard<std::mutex> lock(g_clientsMutex);
                g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), client), g_clients.end());
            }
            closesocket(client);
            --g_active;
        }).detach();
    }
}

void stopServer() {
    if (g_listener == INVALID_SOCKET && !g_acceptThread.joinable()) return;
    g_stopping = true;
    if (g_listener != INVALID_SOCKET) closesocket(g_listener);  // débloque accept()
    g_listener = INVALID_SOCKET;
    if (g_acceptThread.joinable()) g_acceptThread.join();
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        for (SOCKET c : g_clients) ::shutdown(c, SD_BOTH);  // débloque recv()
    }
    // Les connexions en cours peuvent attendre une réponse de cette fenêtre : on la laisse répondre (« occupé »).
    const DWORD start = GetTickCount();
    while (g_active > 0 && GetTickCount() - start < 2000) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_SENDMESSAGE);
        MSG m;
        PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
}

// Renvoie un message d'erreur, vide si le serveur écoute.
std::wstring startServer(int port) {
    stopServer();
    if (!g_wsa) {
        WSADATA data;
        g_wsa = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!g_wsa) return L"Le réseau de Windows est inaccessible.";
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return L"Le réseau de Windows est inaccessible.";
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(u_short(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    // Réservé aux tests : écouter seulement ce PC (127.0.0.1), sans demande d'autorisation du pare-feu.
    const std::wstring only = iniRead(kSection, L"adresse");
    if (!only.empty()) InetPtonW(AF_INET, only.c_str(), &address.sin_addr);
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(s, SOMAXCONN) != 0) {
        const int error = WSAGetLastError();
        closesocket(s);
        if (error == WSAEADDRINUSE || error == WSAEACCES)
            return L"Le port " + std::to_wstring(port) + L" est déjà pris par un autre logiciel. Change-le dans SoundZ.ini "
                                                         L"([telecommande] port=…), puis réactive la télécommande.";
        return L"Impossible d'ouvrir la télécommande (erreur réseau " + std::to_wstring(error) + L").";
    }
    g_listener = s;
    g_stopping = false;
    g_acceptThread = std::thread(acceptLoop);
    return L"";
}

// ---------------------------------------------------------------- réseau local : adresse du PC

bool privateAddress(const in_addr& a) {
    const uint32_t ip = ntohl(a.s_addr);
    return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u);
}

// L'adresse IPv4 du PC sur la box : une carte réseau active, avec une passerelle, qui n'est pas virtuelle.
std::wstring localAddress() {
    ULONG size = 16 * 1024;
    std::vector<BYTE> buffer(size);
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) return L"";
    std::wstring best;
    int bestScore = -1;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL) continue;
        const std::wstring description = std::wstring(a->Description ? a->Description : L"") + L" " +
                                         (a->FriendlyName ? a->FriendlyName : L"");
        bool isVirtual = false;
        for (const wchar_t* v : {L"Virtual", L"Hyper-V", L"VMware", L"VirtualBox", L"vEthernet", L"WSL", L"VPN", L"TAP-", L"Loopback",
                                 L"Tailscale", L"ZeroTier", L"WireGuard", L"Bluetooth"})
            if (contains(description, v)) isVirtual = true;
        if (isVirtual) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const in_addr ip = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
            int score = 0;
            if (a->FirstGatewayAddress) score += 4;
            if (privateAddress(ip)) score += 2;
            if (a->IfType == IF_TYPE_IEEE80211 || a->IfType == IF_TYPE_ETHERNET_CSMACD) score += 1;
            if ((ntohl(ip.s_addr) >> 16) == ((169u << 8) | 254u)) score -= 10;  // adresse « de secours » : pas de box
            if (score > bestScore) {
                wchar_t text[INET_ADDRSTRLEN] = {};
                InetNtopW(AF_INET, &ip, text, INET_ADDRSTRLEN);
                best = text;
                bestScore = score;
            }
        }
    }
    return bestScore >= 0 ? best : L"";
}

// Réseau réglé en « public » dans Windows : le pare-feu y bloque les téléphones.
bool onPublicNetworkOnly() {
    static const GUID kClsidNetworkListManager = {0xDCB00C01, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    static const GUID kIidNetworkListManager = {0xDCB00000, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    INetworkListManager* manager = nullptr;
    if (FAILED(CoCreateInstance(kClsidNetworkListManager, nullptr, CLSCTX_ALL, kIidNetworkListManager,
                                reinterpret_cast<void**>(&manager))))
        return false;
    bool anyPublic = false, anyPrivate = false;
    IEnumNetworks* networks = nullptr;
    if (SUCCEEDED(manager->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &networks)) && networks) {
        INetwork* network = nullptr;
        ULONG fetched = 0;
        while (networks->Next(1, &network, &fetched) == S_OK && network) {
            NLM_NETWORK_CATEGORY category = NLM_NETWORK_CATEGORY_PUBLIC;
            if (SUCCEEDED(network->GetCategory(&category))) (category == NLM_NETWORK_CATEGORY_PUBLIC ? anyPublic : anyPrivate) = true;
            network->Release();
            network = nullptr;
        }
        networks->Release();
    }
    manager->Release();
    return anyPublic && !anyPrivate;
}

// ---------------------------------------------------------------- état (fil de la fenêtre)

struct Phone {
    std::string ip;
    DWORD tick;
};

struct Page {
    HWND title = nullptr, intro = nullptr, toggle = nullptr, qr = nullptr, address = nullptr, steps = nullptr, status = nullptr;
    HWND newCode = nullptr, back = nullptr;
    bool shown = false;
};
Page pg;

bool g_enabled = false;
bool g_busy = false;  // une requête est en cours de traitement (pas de traitement imbriqué)
int g_port = kDefaultPort;
std::string g_code;  // code secret, dans le lien du QR code
std::wstring g_ip, g_error, g_notice;
DWORD g_noticeTick = 0;
bool g_publicNetwork = false;
std::string g_url;
std::vector<bool> g_modules;  // QR code : g_qrSize × g_qrSize cases
int g_qrSize = 0;
std::vector<Phone> g_phones;
std::string g_icon;  // icône de l'écran d'accueil (PNG)

std::string newCode() {
    unsigned char bytes[12] = {};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = static_cast<unsigned char>((t.QuadPart >> (i * 5)) ^ (GetTickCount() >> i));
    }
    static const char hex[] = "0123456789abcdef";
    std::string code;
    for (unsigned char b : bytes) {
        code += hex[b >> 4];
        code += hex[b & 15];
    }
    return code;
}

void buildQr() {
    g_modules.clear();
    g_qrSize = 0;
    g_url.clear();
    if (g_ip.empty() || g_code.empty()) return;
    g_url = "http://" + narrow(g_ip) + ":" + std::to_string(g_port) + "/?k=" + g_code;
    try {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(g_url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        g_qrSize = qr.getSize();
        g_modules.resize(size_t(g_qrSize) * size_t(g_qrSize));
        for (int y = 0; y < g_qrSize; ++y)
            for (int x = 0; x < g_qrSize; ++x) g_modules[size_t(y) * size_t(g_qrSize) + size_t(x)] = qr.getModule(x, y);
    } catch (...) {
        g_qrSize = 0;
        g_modules.clear();
    }
}

void refreshNetwork() {
    g_ip = localAddress();
    g_publicNetwork = onPublicNetworkOnly();
    buildQr();
    setText(pg.address, g_ip.empty() ? L"" : L"http://" + g_ip + L":" + std::to_wstring(g_port));
    if (pg.qr) InvalidateRect(pg.qr, nullptr, FALSE);
}

void updateStatus() {
    std::wstring text;
    if (!g_enabled) {
        text = L"Désactivée : aucun téléphone ne peut se connecter à SoundZ.";
    } else if (!g_error.empty()) {
        text = g_error;
    } else if (g_ip.empty()) {
        text = L"Aucun réseau trouvé : connecte ce PC à ta box (Wi-Fi ou câble).";
    } else {
        const DWORD now = GetTickCount();
        std::vector<std::string> seen;
        for (const Phone& phone : g_phones)
            if (now - phone.tick < kPhoneSeenMs) seen.push_back(phone.ip);
        if (!g_notice.empty() && now - g_noticeTick < 6000) text = g_notice;
        else if (seen.size() == 1) text = L"● Téléphone relié (" + widen(seen[0]) + L").";
        else if (seen.size() > 1) text = L"● " + std::to_wstring(seen.size()) + L" téléphones reliés.";
        else if (g_publicNetwork)
            text = L"Ton réseau est réglé en « public » dans Windows : il bloque le téléphone. Paramètres → Réseau et "
                   L"Internet → Wi-Fi (ou Ethernet) → ton réseau → « Réseau privé ».";
        else
            text = L"En attente du téléphone. Si Windows demande l'autorisation, coche « Réseaux privés », puis Autoriser.";
    }
    setText(pg.status, text);
}

bool statusIsError() {
    if (!g_enabled) return false;
    if (!g_error.empty() || g_ip.empty()) return true;
    const DWORD now = GetTickCount();
    for (const Phone& phone : g_phones)
        if (now - phone.tick < kPhoneSeenMs) return false;
    return g_publicNetwork && !(!g_notice.empty() && now - g_noticeTick < 6000);
}

void enable(bool on) {
    g_enabled = on;
    g_error.clear();
    if (!on) {
        stopServer();
        g_phones.clear();
    } else {
        if (g_code.empty()) {
            g_code = newCode();
            iniWrite(kSection, L"code", widen(g_code));
        }
        g_port = std::clamp(iniReadInt(kSection, L"port", kDefaultPort), 1024, 65535);
        g_error = startServer(g_port);
        refreshNetwork();
    }
    updateStatus();
}

// Icône de l'écran d'accueil : le carré « SZ » de SoundZ (180 × 180 ; iOS arrondit lui-même les coins).
const std::string& homeIcon() {
    if (!g_icon.empty()) return g_icon;
    const int size = 180;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        DeleteDC(dc);
        return g_icon;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    RECT all = {0, 0, size, size};
    FillRect(dc, &all, theme::brush(theme::kAccent));
    HFONT font = CreateFontW(-size * 11 / 20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, L"SZ", 2, &all, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();
    auto* px = static_cast<uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) px[i] |= 0xFF000000;  // opaque partout
    soundimg::encodePng(px, size, size, false, g_icon);
    SelectObject(dc, oldFont);
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return g_icon;
}

// ---------------------------------------------------------------- requêtes du téléphone (fil de la fenêtre)

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string param(const std::string& encoded, const std::string& name) {
    size_t at = 0;
    while (at <= encoded.size()) {
        size_t end = encoded.find('&', at);
        if (end == std::string::npos) end = encoded.size();
        const std::string pair = encoded.substr(at, end - at);
        const size_t eq = pair.find('=');
        if (pair.substr(0, eq) == name) {
            std::string raw = eq == std::string::npos ? "" : pair.substr(eq + 1), value;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '+') value += ' ';
                else if (raw[i] == '%' && i + 2 < raw.size() && hexValue(raw[i + 1]) >= 0 && hexValue(raw[i + 2]) >= 0) {
                    value += char(hexValue(raw[i + 1]) * 16 + hexValue(raw[i + 2]));
                    i += 2;
                } else value += raw[i];
            }
            return value;
        }
        at = end + 1;
    }
    return "";
}

// Comparaison en temps constant : la durée ne trahit pas le code.
bool validCode(const std::string& code) {
    if (g_code.empty() || code.size() != g_code.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < code.size(); ++i) diff |= static_cast<unsigned char>(code[i] ^ g_code[i]);
    return diff == 0;
}

std::string stateJson() {
    return "{\"music\":" + musicpage::remoteJson() + ",\"voice\":" + voicepage::remoteJson() + ",\"board\":" + soundpage::remoteJson() + "}";
}

void notePhone(const std::string& ip) {
    const DWORD now = GetTickCount();
    for (Phone& phone : g_phones)
        if (phone.ip == ip) {
            phone.tick = now;
            return;
        }
    g_phones.erase(std::remove_if(g_phones.begin(), g_phones.end(), [now](const Phone& p) { return now - p.tick > 60000; }),
                   g_phones.end());
    g_phones.push_back({ip, now});
    if (pg.shown) updateStatus();
}

void handle(Request& r) {
    const std::string code = param(r.query, "k");
    if (r.path == "/" || r.path == "/index.html") {
        r.status = 200;
        r.type = "text/html; charset=utf-8";
        r.cache = "no-cache";
        r.content = kPageHtml;
        return;
    }
    if (r.path == "/icon.png" || r.path == "/apple-touch-icon.png" || r.path == "/favicon.ico") {
        r.content = homeIcon();
        r.status = r.content.empty() ? 404 : 200;
        r.type = "image/png";
        r.cache = "max-age=86400";
        return;
    }
    if (r.path == "/manifest.webmanifest") {
        const std::string start = validCode(code) ? "/?k=" + code : "/";
        r.status = 200;
        r.type = "application/manifest+json; charset=utf-8";
        r.content = "{\"name\":\"SoundZ\",\"short_name\":\"SoundZ\",\"start_url\":\"" + start +
                    "\",\"display\":\"standalone\",\"orientation\":\"portrait\",\"background_color\":\"#1e1f22\","
                    "\"theme_color\":\"#1e1f22\",\"icons\":[{\"src\":\"/icon.png\",\"sizes\":\"180x180\",\"type\":\"image/png\"}]}";
        return;
    }
    const bool api = r.path == "/api/state" || r.path == "/api/cmd" || r.path.rfind("/img/", 0) == 0;
    if (!api) {
        r.status = 404;
        r.content = "Introuvable";
        return;
    }
    if (!validCode(code)) {
        r.status = 403;
        r.type = "application/json; charset=utf-8";
        r.content = "{\"error\":\"code\"}";
        return;
    }
    notePhone(r.peer);
    if (r.path.rfind("/img/", 0) == 0) {
        r.status = soundpage::remoteImage(atoi(r.path.c_str() + 5), r.content) ? 200 : 404;
        r.type = "image/png";
        r.cache = "private, max-age=31536000, immutable";  // le lien change avec la version du soundboard
        return;
    }
    if (r.path == "/api/cmd") {
        if (r.method != "POST") {
            r.status = 405;
            return;
        }
        const std::string action = param(r.body, "a");
        const int a = atoi(param(r.body, "v").c_str()), b = atoi(param(r.body, "w").c_str());
        const size_t dot = action.find('.');
        const std::string page = action.substr(0, dot), what = dot == std::string::npos ? "" : action.substr(dot + 1);
        if (page == "music") musicpage::remoteCommand(what, a, b);
        else if (page == "voice") voicepage::remoteCommand(what, a, b);
        else if (page == "sb") soundpage::remoteCommand(what, a, b);
    }
    r.status = 200;
    r.type = "application/json; charset=utf-8";
    r.content = stateJson();
}

// ---------------------------------------------------------------- dessin du QR code

void drawQr(const DRAWITEMSTRUCT* d) {
    RECT rc = d->rcItem;
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC dc = CreateCompatibleDC(d->hDC);
    HBITMAP canvas = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ old = SelectObject(dc, canvas);
    RECT all = {0, 0, w, h};
    FillRect(dc, &all, theme::brush(theme::kBg));
    if (g_qrSize > 0) {
        const int side = std::min(w, h);
        const int cell = std::max(1, side / (g_qrSize + 6));  // 3 cases blanches tout autour
        const int qrPx = cell * g_qrSize, pad = (side - qrPx) / 2;
        const int left = (w - side) / 2, top = (h - side) / 2;
        RECT paper = {left, top, left + side, top + side};
        theme::roundRect(dc, paper, float(S(10)), RGB(255, 255, 255));
        HBRUSH black = theme::brush(RGB(0, 0, 0));
        for (int y = 0; y < g_qrSize; ++y)
            for (int x = 0; x < g_qrSize; ++x) {
                if (!g_modules[size_t(y) * size_t(g_qrSize) + size_t(x)]) continue;
                RECT m = {left + pad + x * cell, top + pad + y * cell, left + pad + (x + 1) * cell, top + pad + (y + 1) * cell};
                FillRect(dc, &m, black);
            }
    }
    BitBlt(d->hDC, rc.left, rc.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(canvas);
    DeleteDC(dc);
}

}  // namespace

// ---------------------------------------------------------------- interface publique

void create() {
    pg.title = makeControl(L"STATIC", L"Télécommande sur téléphone", SS_LEFT | SS_CENTERIMAGE);
    pg.intro = makeControl(L"STATIC",
                           L"Commande SoundZ depuis ton téléphone : le soundboard, la voix et la musique. Le téléphone doit "
                           L"être sur la même box que ce PC (son Wi-Fi). Rien ne passe par Internet.",
                           SS_LEFT);
    pg.toggle = theme::button(L"Activer la télécommande", theme::Kind::kToggle, IDC_REMOTE_ON);
    pg.qr = makeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_REMOTE_QR);
    pg.address = makeControl(L"STATIC", L"", SS_CENTER);
    pg.steps = makeControl(L"STATIC",
                           L"1. Scanne ce QR code avec l'appareil photo du téléphone.\n"
                           L"2. Garde la page sur l'écran d'accueil, comme une appli. Sur iPhone : Partager → « Sur l'écran "
                           L"d'accueil ». Sur Android : ⋮ → « Ajouter à l'écran d'accueil ».",
                           SS_LEFT);
    pg.status = makeControl(L"STATIC", L"", SS_LEFT);
    pg.newCode = theme::button(L"Nouveau code", theme::Kind::kButton, IDC_REMOTE_NEWCODE);
    pg.back = theme::button(L"← Retour", theme::Kind::kButton, kBackId);
}

int layout(int top, bool shown) {
    pg.shown = shown;
    const int margin = 20, width = ui.width, inner = width - 2 * margin;
    const bool ready = g_enabled && g_error.empty() && g_qrSize > 0;
    int y = top + 6;
    place(pg.title, margin, y, inner, 28, ui.font);
    y += 32;
    place(pg.intro, margin, y, inner, 50, ui.smallFont);
    y += 56;
    place(pg.toggle, margin, y, inner, 22, ui.smallFont);
    y += 32;
    if (ready) {
        place(pg.qr, (width - 196) / 2, y, 196, 196, ui.font);
        y += 202;
        place(pg.address, margin, y, inner, 22, ui.font);
        y += 28;
        place(pg.steps, margin, y, inner, 66, ui.smallFont);
        y += 72;
    }
    place(pg.status, margin, y, inner, 50, ui.smallFont);
    y += 56;
    place(pg.back, margin, y, 150, 32, ui.font);
    place(pg.newCode, width - margin - 150, y, 150, 32, ui.font);
    y += 44;
    for (HWND h : {pg.title, pg.intro, pg.toggle, pg.status, pg.back}) ShowWindow(h, shown ? SW_SHOW : SW_HIDE);
    for (HWND h : {pg.qr, pg.address, pg.steps}) ShowWindow(h, shown && ready ? SW_SHOW : SW_HIDE);
    ShowWindow(pg.newCode, shown && g_enabled ? SW_SHOW : SW_HIDE);
    return y;
}

bool onCommand(WPARAM wp, LPARAM) {
    if (HIWORD(wp) != BN_CLICKED) return false;
    switch (LOWORD(wp)) {
    case IDC_REMOTE_ON: {
        const bool on = theme::isChecked(pg.toggle);  // le thème vient de basculer l'interrupteur
        iniWrite(kSection, L"active", on ? L"1" : L"0");
        enable(on);
        PostMessageW(ui.wnd, voicepage::WM_APP_RELAYOUT, 0, 0);
        return true;
    }
    case IDC_REMOTE_NEWCODE:
        g_code = newCode();
        iniWrite(kSection, L"code", widen(g_code));
        g_phones.clear();
        refreshNetwork();
        g_notice = L"Nouveau code : les téléphones déjà reliés doivent scanner ce nouveau QR code.";
        g_noticeTick = GetTickCount();
        updateStatus();
        return true;
    }
    return false;
}

bool onDrawItem(const DRAWITEMSTRUCT* item) {
    if (item->hwndItem != pg.qr) return false;
    drawQr(item);
    return true;
}

bool isStatus(HWND h, bool& error) {
    if (h == pg.status) {
        error = statusIsError();
        return true;
    }
    if (h == pg.intro || h == pg.steps || h == pg.address) {
        error = false;
        return true;
    }
    return false;
}

void pageShown() {
    if (g_enabled && g_error.empty()) refreshNetwork();
    updateStatus();
}

void onTimer() {
    if (pg.shown) updateStatus();
}

void startup() {
    g_code = narrow(iniRead(kSection, L"code"));
    if (g_code.size() < 16) g_code.clear();
    const bool on = iniReadInt(kSection, L"active", 0) != 0;
    SendMessageW(pg.toggle, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    if (on) enable(true);
    else updateStatus();
}

void shutdown() {
    stopServer();
    if (g_wsa) WSACleanup();
    g_wsa = false;
}

LRESULT onMessage(LPARAM request) {
    auto* r = reinterpret_cast<Request*>(request);
    if (g_stopping || g_busy || !g_enabled) {
        r->status = 503;
        r->content = "Occupé";
        return 0;
    }
    g_busy = true;
    handle(*r);
    g_busy = false;
    return 0;
}

}  // namespace remote
