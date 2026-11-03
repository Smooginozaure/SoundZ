#include "sound_page.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "default_output.h"
#include "miniaudio/miniaudio.h"
#include "sound_dsp.h"
#include "sound_image.h"
#include "sound_mixer.h"
#include "sound_claude.h"
#include "sound_replay.h"
#include "ui.h"
#include "util.h"
#include "voice_page.h"

namespace soundpage {
namespace {

constexpr int kMaxSounds = 24;
constexpr int kColumns = 5;
constexpr int kTileW = 67, kTileH = 100, kGap = 6;  // DIP
constexpr int kImageDip = 44;
constexpr int kPreviewDip = 72;
constexpr int kPreviewTag = 63;
constexpr int kMaxWaitSeconds = 20;  // attente du son après « Capturer »
constexpr int kMaxDuration = 10;
constexpr int kMaxPending = 5;  // captures « à trier »
constexpr int kBipTag = 62;
constexpr int kReplayHotkeyId = 0x4FFE;
constexpr int kStopHotkeyId = 0x4FFF;
constexpr int kHotkeyBase = 0x5000;
constexpr WORD kStopHotkey = MAKEWORD('0', HOTKEYF_CONTROL | HOTKEYF_SHIFT);
constexpr WORD kDefaultReplayHotkey = MAKEWORD(VK_F12, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
constexpr size_t kMinSelection = sound::kRate / 10;  // 0,1 s
constexpr wchar_t kSection[] = L"soundboard";

enum ControlId : int {
    IDC_ADD = 499,
    IDC_TILE0 = 500,  // … IDC_TILE0 + kMaxSounds - 1
    IDC_VOLUME = 600,
    IDC_STOP,
    IDC_HEAR,
    IDC_NAME,
    IDC_DURATION,
    IDC_CAPTURE,
    IDC_LISTEN,
    IDC_CLEAN,
    IDC_EMOJI,
    IDC_PICK,
    IDC_HOTKEY,
    IDC_NOHOTKEY,
    IDC_SOUNDVOL,
    IDC_SAVE,
    IDC_CANCEL,
    IDC_PREVIEW,
    IDC_WAVE,
    IDC_REPLAYKEY,
    IDC_REPLAYNONE,
    IDC_AIDRAW,
    IDC_AIEMOJI,
    IDC_PENDING0 = 700,  // … IDC_PENDING0 + kMaxPending - 1
};

enum MenuId : int { kMenuPlay = 1, kMenuEdit, kMenuLeft, kMenuRight, kMenuDelete };

const wchar_t* const kEmojis[] = {L"💨", L"💩", L"😂", L"🤣", L"😱", L"👏", L"🥁", L"🎺", L"📯", L"🚨", L"💀", L"🤡",
                                  L"🐐", L"🐔", L"🦆", L"🔥", L"🎉", L"🤔", L"😎", L"🙄", L"🍑", L"👻", L"⏰", L"🔊"};

using Samples = std::shared_ptr<const std::vector<float>>;

struct Sound {
    int uid = 0;  // fixe : sert aux noms de fichiers et aux raccourcis
    std::wstring name;
    std::wstring emoji;  // vide si l'image vient d'un fichier
    int volume = 100;    // %
    int cleaning = 60;   // nettoyage, %
    WORD hotkey = 0;     // format du contrôle « raccourci » : touche + modificateurs HOTKEYF_*
    bool hotkeyOk = true;
    Samples samples;
    soundimg::Image image;
    HBITMAP bitmap = nullptr;
    int bitmapPx = 0;
};

struct Recorder {
    enum State { kIdle, kWaiting, kRecording };
    State state = kIdle;
    ma_device device;
    bool deviceInit = false;
    ProcessLoopback process;    // tous les programmes sauf SoundZ (sinon, en secours, la sortie par défaut)
    std::vector<float> buffer;  // stéréo entrelacé
    size_t capacity = 0;        // en trames
    std::atomic<size_t> frames{0};
    size_t scanned = 0, trigger = 0;
    DWORD startTick = 0, triggerTick = 0;
    int seconds = 3;
};

struct Player {
    ma_device device;
    bool init = false;
    std::atomic<bool> lost{false};
    std::atomic<bool> stopping{false};
};

struct Editor {
    bool open = false;
    int index = -1;          // son modifié ; -1 = nouveau son
    int pending = -1;        // capture « à trier » en cours de découpage
    bool askedClaude = false;  // une image a été demandée à Claude pour cet éditeur
    std::wstring claudeError;
    std::vector<float> raw;  // capture brute (mono) : on peut régler le nettoyage après coup
    bool rawChanged = false; // nouvelle capture, à enregistrer
    const float* previewed = nullptr;  // son joué par « Écouter » (à garder tant qu'il peut jouer)
    Samples samples;         // son prêt
    soundimg::Image image;
    HBITMAP bitmap = nullptr;
    int bitmapPx = 0;
    std::wstring message;
    bool messageError = false;
    // Passage gardé dans la capture brute (en échantillons), et la forme du son à l'écran.
    size_t selStart = 0, selEnd = 0;
    std::vector<float> peaks;
    int peaksWidth = 0;
    int drag = 0;  // 1 : poignée de début, 2 : poignée de fin, 3 : nouvelle sélection
    size_t anchor = 0, dragStart = 0, dragEnd = 0;
};

// Une capture des 10 dernières secondes, en attente de découpage (fichier dans Sons\a_trier).
struct Pending {
    std::wstring file;
    std::wstring time;  // « 14:32:05 »
};

struct Page {
    // Les boutons
    HWND add = nullptr, tiles[kMaxSounds] = {}, empty = nullptr;
    HWND volumeLabel = nullptr, volume = nullptr, stop = nullptr, hear = nullptr, status = nullptr, hint = nullptr;
    // L'éditeur d'un son
    HWND title = nullptr, nameLabel = nullptr, name = nullptr, durationLabel = nullptr, duration = nullptr;
    HWND capture = nullptr, listen = nullptr, progress = nullptr, message = nullptr, cleanLabel = nullptr, clean = nullptr;
    HWND imageLabel = nullptr, preview = nullptr, emoji = nullptr, pick = nullptr, imageHint = nullptr;
    HWND aiDraw = nullptr, aiEmoji = nullptr;  // ✨ image proposée par Claude
    HWND hotkeyLabel = nullptr, hotkey = nullptr, noHotkey = nullptr, soundVolLabel = nullptr, soundVol = nullptr;
    HWND save = nullptr, cancel = nullptr;
    HWND wave = nullptr, selLabel = nullptr;
    HWND pendingTiles[kMaxPending] = {};
    HWND replayLabel = nullptr, replayKey = nullptr, replayNone = nullptr;
    HFONT emojiFont = nullptr, tinyFont = nullptr;
    UINT fontDpi = 0;
    bool shown = false;

    std::vector<Sound> sounds;
    std::vector<Samples> retired;  // sons supprimés : gardés en mémoire au cas où ils jouent encore
    std::vector<int> registered;   // raccourcis enregistrés auprès de Windows
    Editor editor;
    Recorder rec;
    Player player;
    ma_context context;
    bool contextReady = false;
    std::wstring folder;
    std::wstring hotkeyError;
    uint64_t lastPlaying = 0;
    bool lastVoice = false;

    // Replay : les 10 dernières secondes de la sortie par défaut, en permanence.
    ReplayBuffer replay;
    bool replayAllApps = true;  // tous les programmes sauf SoundZ (sinon : la sortie par défaut)
    std::wstring replayEndpoint;
    DWORD replayCheck = 0;
    WORD replayHotkey = kDefaultReplayHotkey;
    std::vector<Pending> pending;
    soundimg::Image scissors;
    HBITMAP scissorsBitmap = nullptr;
    int scissorsPx = 0;
    std::vector<float> bipOk, bipEmpty;
    bool blink = false;
    DWORD blinkTick = 0;
    std::wstring notice;  // message passager (« capture gardée »…)
    bool noticeError = false;
    DWORD noticeTick = 0;
};

Page p;
SoundMixer g_cable;  // vers le micro virtuel (mélangé par le moteur de la voix)
SoundMixer g_local;  // vers le casque (lecteur de cette page)

void show(HWND h, bool visible) { ShowWindow(h, visible ? SW_SHOW : SW_HIDE); }

std::wstring text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}

std::wstring trim(std::wstring s) {
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && iswspace(s[i])) ++i;
    return s.substr(i);
}

void relayout() { PostMessageW(ui.wnd, voicepage::WM_APP_RELAYOUT, 0, 0); }

// ---------------------------------------------------------------- raccourcis clavier

// Nom lisible d'un raccourci (« Ctrl+Maj+1 ») : le même que dans la case de raccourci du thème.
std::wstring hotkeyText(WORD hk) { return LOBYTE(hk) ? theme::hotkeyText(hk) : L""; }

bool registerOne(int id, WORD hk) {
    BYTE mods = HIBYTE(hk);
    UINT m = MOD_NOREPEAT;
    if (mods & HOTKEYF_CONTROL) m |= MOD_CONTROL;
    if (mods & HOTKEYF_SHIFT) m |= MOD_SHIFT;
    if (mods & HOTKEYF_ALT) m |= MOD_ALT;
    if (!RegisterHotKey(ui.wnd, id, m, LOBYTE(hk))) return false;
    p.registered.push_back(id);
    return true;
}

void unregisterHotkeys() {
    for (int id : p.registered) UnregisterHotKey(ui.wnd, id);
    p.registered.clear();
}

void registerHotkeys() {
    unregisterHotkeys();
    p.hotkeyError.clear();
    for (Sound& s : p.sounds) {
        s.hotkeyOk = !LOBYTE(s.hotkey) || registerOne(kHotkeyBase + s.uid, s.hotkey);
        if (!s.hotkeyOk) {
            if (!p.hotkeyError.empty()) p.hotkeyError += L", ";
            p.hotkeyError += hotkeyText(s.hotkey);
        }
    }
    if (LOBYTE(p.replayHotkey) && !registerOne(kReplayHotkeyId, p.replayHotkey)) {
        if (!p.hotkeyError.empty()) p.hotkeyError += L", ";
        p.hotkeyError += hotkeyText(p.replayHotkey) + L" (10 dernières secondes)";
    }
    registerOne(kStopHotkeyId, kStopHotkey);
}

// Prochain Ctrl+Maj+1…9 libre, pour un nouveau son.
WORD freeHotkey() {
    for (int d = 1; d <= 9; ++d) {
        WORD hk = MAKEWORD('0' + d, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
        bool used = false;
        for (const Sound& s : p.sounds) used = used || s.hotkey == hk;
        if (!used) return hk;
    }
    return 0;
}

// ---------------------------------------------------------------- lecture

// Numéro d'un son pour le mélangeur (0 à 59) : 62 et 63 servent au bip et à « Écouter », jamais à un bouton.
int soundTag(const Sound& s) { return s.uid % 60; }

Sound* findUid(int uid) {
    for (Sound& s : p.sounds)
        if (s.uid == uid) return &s;
    return nullptr;
}

void onPlayerData(ma_device* device, void* output, const void*, ma_uint32 frames) {
    (void)device;
    float* out = static_cast<float*>(output);
    std::fill(out, out + frames, 0.0f);
    g_local.mix(out, int(frames));
    for (ma_uint32 i = 0; i < frames; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void onPlayerNotification(const ma_device_notification* n) {
    if (n->type == ma_device_notification_type_stopped && !p.player.stopping) p.player.lost = true;
}

void closePlayer() {
    if (!p.player.init) return;
    p.player.stopping = true;
    ma_device_uninit(&p.player.device);
    p.player.init = false;
}

// Lecteur sur le casque : la sortie choisie pour le retour de la voix, sinon là où l'on écoute (la sortie par
// défaut au lancement), sinon la première vraie sortie.
bool openPlayer() {
    if (p.player.init && !p.player.lost) return true;
    closePlayer();
    if (!p.contextReady) return false;
    ma_device_info *playback = nullptr, *capture = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    const ma_device_id* id = nullptr;
    std::wstring name;
    if (ma_context_get_devices(&p.context, &playback, &playbackCount, &capture, &captureCount) == MA_SUCCESS) {
        for (const std::wstring& wanted : {iniRead(L"voix", L"retour"), ui.listenOutput}) {
            for (ma_uint32 i = 0; i < playbackCount && !id; ++i) {
                if (!wanted.empty() && sameText(reinterpret_cast<const wchar_t*>(playback[i].id.wasapi), wanted)) {
                    id = &playback[i].id;
                    name = widen(playback[i].name);
                }
            }
        }
        for (ma_uint32 i = 0; i < playbackCount && !id; ++i) {
            if (!isVirtualDevice(widen(playback[i].name))) {
                id = &playback[i].id;
                name = widen(playback[i].name);
            }
        }
    }
    setText(p.hear, L"M'entendre aussi dans le casque" + (name.empty() ? L"" : L" (" + shortDeviceName(name) + L")"));
    ma_device_config c = ma_device_config_init(ma_device_type_playback);
    c.playback.pDeviceID = id;
    c.playback.format = ma_format_f32;
    c.playback.channels = 1;
    c.sampleRate = sound::kRate;
    c.periodSizeInMilliseconds = 10;
    c.dataCallback = onPlayerData;
    c.notificationCallback = onPlayerNotification;
    c.wasapi.noAutoStreamRouting = id ? MA_TRUE : MA_FALSE;
    g_local.resetIdle();
    p.player.lost = false;
    p.player.stopping = false;
    if (ma_device_init(&p.context, &c, &p.player.device) != MA_SUCCESS) return false;
    p.player.init = true;
    if (ma_device_start(&p.player.device) != MA_SUCCESS) {
        closePlayer();
        return false;
    }
    return true;
}

bool hearMyself() { return SendMessageW(p.hear, BM_GETCHECK, 0, 0) == BST_CHECKED; }

void playSound(const Sound& s) {
    if (!s.samples || s.samples->empty()) return;
    float gain = float(s.volume) / 100.0f;
    int length = int(s.samples->size());
    if (voicepage::running()) g_cable.play(s.samples->data(), length, gain, soundTag(s));
    if (hearMyself() && openPlayer()) g_local.play(s.samples->data(), length, gain, soundTag(s));
}

void stopAllSounds() {
    g_cable.stopAll();
    g_local.stopAll();
}

void applyVolume(bool save) {
    int v = sliderPos(p.volume);
    float gain = float(v * v) / 10000.0f;  // courbe « à l'oreille »
    g_cable.setVolume(gain);
    g_local.setVolume(gain);
    setText(p.volumeLabel, L"Volume : " + std::to_wstring(v) + L" %");
    if (save) iniWrite(kSection, L"volume", std::to_wstring(v));
}

void updateStatus();

// Message passager sous les boutons (quelques secondes).
void setNotice(const std::wstring& text, bool error) {
    p.notice = text;
    p.noticeError = error;
    p.noticeTick = GetTickCount();
    updateStatus();
}

// Petits « bips » de confirmation, dans le casque seulement (jamais dans le micro).
void makeBips() {
    auto tone = [](std::vector<float>& out, double hz, double seconds, float level) {
        size_t n = size_t(seconds * sound::kRate), fade = sound::kRate / 200;
        size_t start = out.size();
        for (size_t i = 0; i < n; ++i) {
            float env = float(std::min({size_t(1) * fade, i, n - 1 - i})) / float(fade);
            out.push_back(level * env * float(std::sin(2 * sound::kPi * hz * double(i) / sound::kRate)));
        }
        (void)start;
    };
    p.bipOk.clear();
    tone(p.bipOk, 880, 0.06, 0.25f);
    tone(p.bipOk, 1320, 0.09, 0.25f);
    p.bipEmpty.clear();
    tone(p.bipEmpty, 330, 0.18, 0.25f);
}

void bip(bool ok) {
    const std::vector<float>& b = ok ? p.bipOk : p.bipEmpty;
    if (!b.empty() && openPlayer()) g_local.play(b.data(), int(b.size()), 1.0f, kBipTag);
}

// Le replay suit la sortie par défaut de Windows (elle change quand l'onglet Musique démarre ou s'arrête).
// Le replay écoute tous les programmes sauf SoundZ (YouTube, Discord, jeux…, quelle que soit leur sortie).
// En secours (Windows trop ancien), seulement la sortie par défaut, qu'il suit quand elle change.
void followDefaultOutput() {
    if (!p.contextReady) return;
    if (p.replayAllApps) {
        if (p.replay.isOpen() && !p.replay.lost()) return;
        if (p.replay.openProcess(GetCurrentProcessId(), true)) return;
        p.replayAllApps = false;
    }
    std::wstring now = getDefaultOutput();
    if (p.replay.isOpen() && !p.replay.lost() && sameText(now, p.replayEndpoint)) return;
    p.replay.close();
    p.replayEndpoint = now;
    ma_device_info *playback = nullptr, *capture = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    const ma_device_id* id = nullptr;
    if (ma_context_get_devices(&p.context, &playback, &playbackCount, &capture, &captureCount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < playbackCount && !id; ++i)
            if (!now.empty() && sameText(reinterpret_cast<const wchar_t*>(playback[i].id.wasapi), now)) id = &playback[i].id;
    }
    p.replay.openDevice(&p.context, id);
}

std::wstring pendingFolder() { return p.folder + L"a_trier\\"; }

void loadPending() {
    p.pending.clear();
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((pendingFolder() + L"capture_*.wav").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do names.push_back(data.cFileName);
        while (FindNextFileW(find, &data));
        FindClose(find);
    }
    std::sort(names.begin(), names.end());  // capture_AAAAMMJJ_HHMMSS : du plus ancien au plus récent
    for (const std::wstring& n : names) {
        Pending item;
        item.file = pendingFolder() + n;
        // capture_20261103_143205.wav -> 14:32:05
        if (n.size() >= 23) item.time = n.substr(17, 2) + L":" + n.substr(19, 2) + L":" + n.substr(21, 2);
        p.pending.push_back(item);
    }
    while (int(p.pending.size()) > kMaxPending) {
        DeleteFileW(p.pending.front().file.c_str());
        p.pending.erase(p.pending.begin());
    }
}

// Le raccourci : on garde les 10 dernières secondes du PC, à découper plus tard.
void captureReplay() {
    std::vector<float> x = p.replay.last(ReplayBuffer::kSeconds);
    float peak = 0;
    for (float s : x) peak = std::max(peak, std::fabs(s));
    if (x.size() < size_t(sound::kRate / 2) || peak < 0.002f) {
        bip(false);
        setNotice(L"Rien à garder : le PC n'a joué aucun son ces 10 dernières secondes.", true);
        return;
    }
    CreateDirectoryW(p.folder.c_str(), nullptr);
    CreateDirectoryW(pendingFolder().c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"capture_%04d%02d%02d_%02d%02d%02d.wav", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    Pending item;
    item.file = pendingFolder() + name;
    wchar_t time[16];
    swprintf(time, 16, L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    item.time = time;
    if (!sound::writeWav(item.file, x)) {
        bip(false);
        setNotice(L"Impossible d'enregistrer la capture dans le dossier « Sons ».", true);
        return;
    }
    // Pas plus de 5 captures en attente : la plus ancienne laisse sa place.
    for (Pending& old : p.pending)
        if (sameText(old.file, item.file)) old.file.clear();  // même seconde : remplacée
    p.pending.erase(std::remove_if(p.pending.begin(), p.pending.end(), [](const Pending& q) { return q.file.empty(); }),
                    p.pending.end());
    while (int(p.pending.size()) >= kMaxPending) {
        DeleteFileW(p.pending.front().file.c_str());
        p.pending.erase(p.pending.begin());
    }
    p.pending.push_back(item);
    bip(true);
    setNotice(L"✂ 10 dernières secondes gardées (" + item.time + L"). Clique sur le bouton « À trier » pour choisir le passage.",
              false);
    relayout();
}

// ---------------------------------------------------------------- fichiers (dossier « Sons » à côté de l'exe)

std::wstring wavPath(int uid) { return p.folder + L"son" + std::to_wstring(uid) + L".wav"; }
std::wstring pngPath(int uid) { return p.folder + L"son" + std::to_wstring(uid) + L".png"; }
std::wstring rawPath(int uid) { return p.folder + L"son" + std::to_wstring(uid) + L"_brut.wav"; }
std::wstring indexPath() { return p.folder + L"sons.ini"; }

// Réécrit la liste. Le fichier est en UTF-16 pour garder les emojis et les accents.
void saveIndex() {
    CreateDirectoryW(p.folder.c_str(), nullptr);
    std::wstring path = indexPath();
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
        fwrite("\xFF\xFE", 1, 2, f);
        fclose(f);
    }
    std::wstring order;
    for (const Sound& s : p.sounds) {
        if (!order.empty()) order += L",";
        order += std::to_wstring(s.uid);
        std::wstring section = L"son" + std::to_wstring(s.uid);
        WritePrivateProfileStringW(section.c_str(), L"nom", s.name.c_str(), path.c_str());
        WritePrivateProfileStringW(section.c_str(), L"emoji", s.emoji.c_str(), path.c_str());
        WritePrivateProfileStringW(section.c_str(), L"volume", std::to_wstring(s.volume).c_str(), path.c_str());
        WritePrivateProfileStringW(section.c_str(), L"nettoyage", std::to_wstring(s.cleaning).c_str(), path.c_str());
        WritePrivateProfileStringW(section.c_str(), L"raccourci", std::to_wstring(s.hotkey).c_str(), path.c_str());
    }
    WritePrivateProfileStringW(L"liste", L"ordre", order.c_str(), path.c_str());
}

std::wstring indexRead(const std::wstring& section, const wchar_t* key) {
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(section.c_str(), key, L"", buffer, 512, indexPath().c_str());
    return buffer;
}

void loadAll() {
    p.sounds.clear();
    std::wstring order = indexRead(L"liste", L"ordre");
    size_t at = 0;
    while (at < order.size() && int(p.sounds.size()) < kMaxSounds) {
        size_t comma = order.find(L',', at);
        if (comma == std::wstring::npos) comma = order.size();
        int uid = _wtoi(order.substr(at, comma - at).c_str());
        at = comma + 1;
        if (uid <= 0 || findUid(uid)) continue;
        Sound s;
        s.uid = uid;
        std::vector<float> samples;
        if (!sound::readWav(wavPath(uid), samples) || samples.empty()) continue;
        s.samples = std::make_shared<const std::vector<float>>(std::move(samples));
        std::wstring section = L"son" + std::to_wstring(uid);
        s.name = indexRead(section, L"nom");
        s.emoji = indexRead(section, L"emoji");
        std::wstring volume = indexRead(section, L"volume");
        s.volume = volume.empty() ? 100 : std::clamp(_wtoi(volume.c_str()), 0, 200);
        s.hotkey = WORD(_wtoi(indexRead(section, L"raccourci").c_str()));
        std::wstring cleaning = indexRead(section, L"nettoyage");
        s.cleaning = cleaning.empty() ? 60 : std::clamp(_wtoi(cleaning.c_str()), 0, 100);
        if (!soundimg::loadFile(pngPath(uid), s.image)) soundimg::renderText(s.emoji.empty() ? L"🔊" : s.emoji, s.image);
        p.sounds.push_back(std::move(s));
    }
}

// ---------------------------------------------------------------- capture du son du PC

void recordStereo(Recorder* r, const float* input, size_t count) {
    size_t f = r->frames.load(std::memory_order_relaxed);
    size_t take = std::min(count, r->capacity - f);
    if (take == 0 || !input) return;
    std::copy(input, input + take * 2, r->buffer.begin() + long(f * 2));
    r->frames.store(f + take, std::memory_order_release);
}

void onLoopback(ma_device* device, void*, const void* input, ma_uint32 count) {
    recordStereo(static_cast<Recorder*>(device->pUserData), static_cast<const float*>(input), count);
}

void onProcessLoopback(void* user, const float* stereo, uint32_t frames) {
    recordStereo(static_cast<Recorder*>(user), stereo, frames);
}

void closeRecorder() {
    if (p.rec.deviceInit) {
        ma_device_uninit(&p.rec.device);
        p.rec.deviceInit = false;
    }
    p.rec.process.stop();
    p.rec.state = Recorder::kIdle;
}

void setMessage(const std::wstring& m, bool error) {
    p.editor.message = m;
    p.editor.messageError = error;
    setText(p.message, m);
    InvalidateRect(p.message, nullptr, TRUE);
}

std::wstring secondsText(size_t frames) {
    wchar_t b[32];
    swprintf(b, 32, L"%.1f s", double(frames) / sound::kRate);
    std::wstring s = b;
    std::replace(s.begin(), s.end(), L'.', L',');
    return s;
}

void updateEditorButtons() {
    int seconds = sliderPos(p.duration);
    setText(p.durationLabel, L"Durée : " + std::to_wstring(seconds) + L" s");
    switch (p.rec.state) {
    case Recorder::kIdle: setText(p.capture, L"● Capturer " + std::to_wstring(seconds) + L" s"); break;
    case Recorder::kWaiting: setText(p.capture, L"■ Annuler"); break;
    case Recorder::kRecording: setText(p.capture, L"■ Arrêter"); break;
    }
    bool ready = p.editor.samples && !p.editor.samples->empty();
    EnableWindow(p.listen, ready && p.rec.state == Recorder::kIdle);
    EnableWindow(p.clean, !p.editor.raw.empty());
    int c = sliderPos(p.clean);
    setText(p.cleanLabel, c == 0 ? std::wstring(L"Nettoyage : aucun") : L"Nettoyage : " + std::to_wstring(c) + L" %");
    setText(p.soundVolLabel, L"Volume du son : " + std::to_wstring(sliderPos(p.soundVol)) + L" %");
    setText(p.save, p.editor.index < 0 ? L"✓ Ajouter au soundboard" : L"✓ Enregistrer");

    // ✨ Claude : seulement s'il est installé et connecté, et quand le son a un nom.
    const claude::Status status = claude::status();
    const bool named = !trim(text(p.name)).empty(), working = claude::busy() && p.editor.askedClaude;
    const bool canAsk = status == claude::Status::kReady && named && !claude::busy();
    EnableWindow(p.aiDraw, canAsk);
    EnableWindow(p.aiEmoji, canAsk);
    std::wstring hint;
    if (working) hint = L"✨ Claude cherche une image pour « " + trim(text(p.name)) + L" »…";
    else if (!p.editor.claudeError.empty()) hint = p.editor.claudeError;
    else if (status == claude::Status::kChecking) hint = L"Emoji (liste ou Win + .) ou image du PC. Recherche de Claude Code…";
    else if (status == claude::Status::kMissing) hint = L"Emoji (liste ou Win + .) ou image du PC. ✨ grisés : Claude Code n'est pas installé.";
    else if (status == claude::Status::kLoggedOut)
        hint = L"Emoji (liste ou Win + .) ou image du PC. ✨ grisés : Claude Code n'est pas connecté.";
    else if (!named) hint = L"Emoji (liste ou Win + .) ou image du PC. Donne un nom au son pour que ✨ Claude en propose une.";
    else hint = L"Emoji (liste ou Win + .), image du PC, ou ✨ image proposée par Claude.";
    setText(p.imageHint, hint);
}

void updateSelectionLabel() {
    const Editor& e = p.editor;
    if (e.raw.empty()) return;
    std::wstring from = secondsText(e.selStart);
    from.resize(from.size() - 2);  // « 7,0 » (sans « s »)
    setText(p.selLabel, L"Gardé : " + from + L" → " + secondsText(e.selEnd) + L"  (" + secondsText(e.selEnd - e.selStart) + L")");
}

// Toute la capture brute est sélectionnée (ou, pour une capture « à trier », ses dernières secondes).
void selectAll(size_t lastSeconds = 0) {
    Editor& e = p.editor;
    e.selEnd = e.raw.size();
    e.selStart = lastSeconds && e.raw.size() > lastSeconds * sound::kRate ? e.raw.size() - lastSeconds * sound::kRate : 0;
    e.peaksWidth = 0;  // forme du son à recalculer
    InvalidateRect(p.wave, nullptr, FALSE);
    updateSelectionLabel();
}

// Nettoie le passage choisi de la capture brute (selon le curseur) et prépare le son.
void processCapture() {
    const int strength = sliderPos(p.clean);
    Editor& e = p.editor;
    e.selEnd = std::min(e.selEnd, e.raw.size());
    e.selStart = std::min(e.selStart, e.selEnd);
    std::vector<float> part(e.raw.begin() + long(e.selStart), e.raw.begin() + long(e.selEnd));
    sound::CleanResult r = sound::clean(std::move(part), float(strength) / 100.0f);
    // L'ancien son a peut-être été lancé par « Écouter » : on le garde en mémoire.
    if (p.editor.samples && p.editor.samples->data() == p.editor.previewed) p.retired.push_back(p.editor.samples);
    if (r.empty) {
        p.editor.samples.reset();
        setMessage(L"La capture est silencieuse. Lance la vidéo, puis recommence.", true);
    } else {
        size_t n = r.samples.size();
        p.editor.samples = std::make_shared<const std::vector<float>>(std::move(r.samples));
        setMessage(L"✓ Son capturé : " + secondsText(n) + (strength > 0 ? L", nettoyé." : L".") +
                       L" Clique sur ▶ Écouter pour vérifier.", false);
    }
    updateEditorButtons();
}

void finishCapture() {
    size_t frames = p.rec.frames.load(std::memory_order_acquire);
    closeRecorder();
    size_t start = p.rec.trigger > sound::kRate / 10 ? p.rec.trigger - sound::kRate / 10 : 0;  // 0,1 s avant
    size_t end = std::min(frames, p.rec.trigger + size_t(p.rec.seconds) * sound::kRate);
    p.editor.raw.clear();
    for (size_t i = start; i < end; ++i) p.editor.raw.push_back(0.5f * (p.rec.buffer[i * 2] + p.rec.buffer[i * 2 + 1]));
    p.editor.rawChanged = true;
    std::vector<float>().swap(p.rec.buffer);
    SendMessageW(p.progress, PBM_SETPOS, 0, 0);
    selectAll();
    processCapture();
    relayout();  // la forme du son apparaît
}

void startCapture() {
    if (!p.contextReady) return;
    closeRecorder();
    Recorder& r = p.rec;
    r.seconds = sliderPos(p.duration);
    r.capacity = size_t(kMaxWaitSeconds + r.seconds + 2) * sound::kRate;
    r.buffer.assign(r.capacity * 2, 0.0f);
    r.frames = 0;
    r.scanned = 0;
    r.trigger = 0;

    // Le son de tous les programmes sauf SoundZ (vidéo, Discord, jeu…, quelle que soit leur sortie).
    // En secours (Windows trop ancien), le « loopback » de la sortie par défaut.
    if (!r.process.start(GetCurrentProcessId(), true, onProcessLoopback, &r)) {
        ma_device_config c = ma_device_config_init(ma_device_type_loopback);
        c.capture.pDeviceID = nullptr;
        c.capture.format = ma_format_f32;
        c.capture.channels = 2;
        c.sampleRate = sound::kRate;
        c.dataCallback = onLoopback;
        c.pUserData = &r;
        if (ma_device_init(&p.context, &c, &r.device) != MA_SUCCESS) {
            setMessage(L"Impossible d'écouter le son du PC.", true);
            return;
        }
        r.deviceInit = true;
        if (ma_device_start(&r.device) != MA_SUCCESS) {
            closeRecorder();
            setMessage(L"Impossible d'écouter le son du PC.", true);
            return;
        }
    }
    r.state = Recorder::kWaiting;
    r.startTick = GetTickCount();
    setMessage(L"En attente du son… Lance la vidéo : la capture démarre dès que le son arrive.", false);
    updateEditorButtons();
}

void cancelCapture() {
    closeRecorder();
    std::vector<float>().swap(p.rec.buffer);
    SendMessageW(p.progress, PBM_SETPOS, 0, 0);
    setMessage(L"Capture annulée.", false);
    updateEditorButtons();
}

void tickRecorder() {
    Recorder& r = p.rec;
    if (r.state == Recorder::kIdle) return;
    size_t frames = r.frames.load(std::memory_order_acquire);
    if (r.state == Recorder::kWaiting) {
        for (; r.scanned < frames; ++r.scanned) {
            if (std::fabs(r.buffer[r.scanned * 2]) > 0.003f || std::fabs(r.buffer[r.scanned * 2 + 1]) > 0.003f) {
                r.state = Recorder::kRecording;
                r.trigger = r.scanned;
                r.triggerTick = GetTickCount();
                updateEditorButtons();
                break;
            }
        }
        if (r.state == Recorder::kWaiting) {
            if (GetTickCount() - r.startTick > DWORD(kMaxWaitSeconds) * 1000) {
                closeRecorder();
                std::vector<float>().swap(r.buffer);
                setMessage(L"Aucun son entendu pendant " + std::to_wstring(kMaxWaitSeconds) +
                               L" s. Lance la vidéo, puis recommence.", true);
                updateEditorButtons();
            }
            return;
        }
    }
    size_t wanted = size_t(r.seconds) * sound::kRate;
    size_t got = frames > r.trigger ? frames - r.trigger : 0;
    SendMessageW(p.progress, PBM_SETPOS, WPARAM(std::min<size_t>(100, got * 100 / wanted)), 0);
    setMessage(L"● Capture en cours… " + secondsText(got), false);
    // Fin : la durée voulue est là (ou plus rien n'arrive : le son du PC s'est arrêté).
    if (got >= wanted || GetTickCount() - r.triggerTick > DWORD(r.seconds) * 1000 + 1500) finishCapture();
}

// ---------------------------------------------------------------- éditeur

void refreshEditorImage() {
    if (p.editor.bitmap) DeleteObject(p.editor.bitmap);
    p.editor.bitmapPx = S(kPreviewDip);
    p.editor.bitmap = soundimg::makeBitmap(p.editor.image, p.editor.bitmapPx);
    InvalidateRect(p.preview, nullptr, TRUE);
}

void imageFromEmoji(const std::wstring& emoji) {
    std::wstring t = trim(emoji);
    if (t.empty()) return;
    soundimg::Image image;
    if (soundimg::renderText(t, image)) {
        p.editor.image = std::move(image);
        refreshEditorImage();
    }
}

void openEditor(int index, int pending = -1) {
    // Claude Code installé ou connecté depuis le lancement ? On revérifie.
    if (claude::status() == claude::Status::kMissing || claude::status() == claude::Status::kLoggedOut) claude::check();
    Editor& e = p.editor;
    e = Editor();
    e.open = true;
    e.index = index;
    e.pending = pending;
    if (pending >= 0) {
        // Une capture « à trier » : on propose ses dernières secondes, à ajuster avec les poignées.
        setText(p.title, L"Découper la capture de " + p.pending[size_t(pending)].time);
        SetWindowTextW(p.name, L"");
        SetWindowTextW(p.emoji, L"💨");
        SendMessageW(p.hotkey, HKM_SETHOTKEY, freeHotkey(), 0);
        setSliderPos(p.soundVol, 100);
        setSliderPos(p.clean, std::clamp(iniReadInt(kSection, L"nettoyage", 60), 0, 100));
        imageFromEmoji(L"💨");
        sound::readWav(p.pending[size_t(pending)].file, e.raw);
        e.rawChanged = true;
        selectAll(size_t(sliderPos(p.duration)));
        processCapture();
        // Sauf si la capture est muette (le message d'erreur reste alors affiché).
        if (!e.messageError)
            e.message = L"Glisse les poignées blanches pour garder le bon passage (ou trace-le à la souris), puis ▶ Écouter.";
    } else if (index >= 0) {
        const Sound& s = p.sounds[size_t(index)];
        e.samples = s.samples;
        e.image = s.image;
        setText(p.title, L"Modifier le son");
        SetWindowTextW(p.name, s.name.c_str());
        SetWindowTextW(p.emoji, s.emoji.c_str());
        SendMessageW(p.hotkey, HKM_SETHOTKEY, s.hotkey, 0);
        setSliderPos(p.soundVol, s.volume);
        setSliderPos(p.clean, s.cleaning);
        sound::readWav(rawPath(s.uid), e.raw);  // capture d'origine, si elle a été gardée
        selectAll();
        e.message = e.raw.empty() ? L"Tu peux recapturer le son, changer l'image, le raccourci ou le volume."
                                  : L"Tu peux régler le nettoyage, recapturer le son, changer l'image ou le raccourci.";
    } else {
        setText(p.title, L"Nouveau son");
        SetWindowTextW(p.name, L"");
        SetWindowTextW(p.emoji, L"💨");
        SendMessageW(p.hotkey, HKM_SETHOTKEY, freeHotkey(), 0);
        setSliderPos(p.soundVol, 100);
        setSliderPos(p.clean, std::clamp(iniReadInt(kSection, L"nettoyage", 60), 0, 100));
        imageFromEmoji(L"💨");
        e.message = L"Clique sur « Capturer », puis lance la vidéo : la capture démarre dès que le son arrive.";
    }
    setText(p.message, e.message);
    refreshEditorImage();
    unregisterHotkeys();  // sinon taper un raccourci déjà pris jouerait le son au lieu de le choisir
    updateEditorButtons();
    relayout();
    SetFocus(index >= 0 || pending >= 0 ? p.name : p.capture);
}

void closeEditor() {
    if (p.rec.state != Recorder::kIdle) cancelCapture();
    g_local.stopAll();
    if (p.editor.bitmap) DeleteObject(p.editor.bitmap);
    p.editor = Editor();
    registerHotkeys();
    relayout();
}

void refreshBitmap(Sound& s, bool force) {
    int px = S(kImageDip);
    if (!force && s.bitmap && s.bitmapPx == px) return;
    if (s.bitmap) DeleteObject(s.bitmap);
    s.bitmap = soundimg::makeBitmap(s.image, px);
    s.bitmapPx = px;
}

void saveEditor() {
    Editor& e = p.editor;
    if (p.rec.state != Recorder::kIdle) return;
    if (!e.samples || e.samples->empty()) {
        setMessage(L"Capture d'abord un son (bouton « Capturer »).", true);
        return;
    }
    Sound s;
    bool isNew = e.index < 0;
    if (!isNew) s = p.sounds[size_t(e.index)];
    else {
        int uid = 1;
        while (findUid(uid) || GetFileAttributesW(wavPath(uid).c_str()) != INVALID_FILE_ATTRIBUTES) ++uid;
        s.uid = uid;
    }
    s.name = trim(text(p.name));
    if (s.name.empty()) s.name = L"Son " + std::to_wstring(p.sounds.size() + (isNew ? 1 : 0));
    s.emoji = trim(text(p.emoji));
    s.volume = sliderPos(p.soundVol);
    s.cleaning = sliderPos(p.clean);
    s.hotkey = LOBYTE(WORD(SendMessageW(p.hotkey, HKM_GETHOTKEY, 0, 0))) ? WORD(SendMessageW(p.hotkey, HKM_GETHOTKEY, 0, 0)) : 0;
    if (e.image.empty()) soundimg::renderText(L"🔊", e.image);
    s.image = e.image;

    CreateDirectoryW(p.folder.c_str(), nullptr);
    if (isNew || s.samples != e.samples) {
        if (!sound::writeWav(wavPath(s.uid), *e.samples)) {
            setMessage(L"Impossible d'enregistrer le son dans le dossier « Sons ».", true);
            return;
        }
        if (s.samples) p.retired.push_back(s.samples);
        s.samples = e.samples;
    }
    // Capture d'origine (le passage choisi seulement), pour régler le nettoyage plus tard.
    if (e.rawChanged && !e.raw.empty())
        sound::writeWav(rawPath(s.uid), std::vector<float>(e.raw.begin() + long(e.selStart), e.raw.begin() + long(e.selEnd)));
    if (e.pending >= 0 && e.pending < int(p.pending.size())) {  // la capture est triée : elle quitte la liste
        DeleteFileW(p.pending[size_t(e.pending)].file.c_str());
        p.pending.erase(p.pending.begin() + e.pending);
    }
    soundimg::savePng(pngPath(s.uid), s.image);
    refreshBitmap(s, true);

    // Un raccourci ne sert qu'à un seul son.
    for (Sound& other : p.sounds)
        if (other.uid != s.uid && s.hotkey && other.hotkey == s.hotkey) other.hotkey = 0;
    if (isNew) {
        p.sounds.push_back(s);
    } else {
        p.sounds[size_t(e.index)] = s;
    }
    saveIndex();
    if (e.bitmap) DeleteObject(e.bitmap);
    p.editor = Editor();
    registerHotkeys();
    for (Sound& x : p.sounds) refreshBitmap(x, false);
    relayout();
}

void deleteSound(int index) {
    Sound& s = p.sounds[size_t(index)];
    std::wstring question = L"Supprimer « " + s.name + L" » du soundboard ?";
    if (MessageBoxW(ui.wnd, question.c_str(), L"Soundboard", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    p.retired.push_back(s.samples);
    if (s.bitmap) DeleteObject(s.bitmap);
    DeleteFileW(wavPath(s.uid).c_str());
    DeleteFileW(pngPath(s.uid).c_str());
    DeleteFileW(rawPath(s.uid).c_str());
    p.sounds.erase(p.sounds.begin() + index);
    saveIndex();
    registerHotkeys();
    relayout();
}

void deletePending(int index) {
    DeleteFileW(p.pending[size_t(index)].file.c_str());
    p.pending.erase(p.pending.begin() + index);
    relayout();
}

// ---------------------------------------------------------------- forme du son et sélection du passage

RECT waveArea(HWND h) {
    RECT r;
    GetClientRect(h, &r);
    InflateRect(&r, -S(6), -S(4));
    return r;
}

int sampleToX(const RECT& r, size_t sample) {
    size_t n = std::max<size_t>(1, p.editor.raw.size());
    return r.left + int(double(sample) / double(n) * (r.right - r.left));
}

size_t xToSample(const RECT& r, int x) {
    double t = std::clamp(double(x - r.left) / std::max(1, int(r.right - r.left)), 0.0, 1.0);
    return size_t(t * double(p.editor.raw.size()));
}

int handleAt(HWND h, int x) {
    RECT r = waveArea(h);
    int xs = sampleToX(r, p.editor.selStart), xe = sampleToX(r, p.editor.selEnd);
    int grab = S(7);
    if (std::abs(x - xe) <= grab && std::abs(x - xe) <= std::abs(x - xs)) return 2;
    if (std::abs(x - xs) <= grab) return 1;
    return 0;
}

LRESULT CALLBACK waveProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    Editor& e = p.editor;
    switch (msg) {
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if (!e.raw.empty()) {
            SetCursor(LoadCursorW(nullptr, handleAt(h, pt.x) ? IDC_SIZEWE : IDC_IBEAM));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (e.raw.empty() || p.rec.state != Recorder::kIdle) break;
        int x = GET_X_LPARAM(lp);
        e.dragStart = e.selStart;
        e.dragEnd = e.selEnd;
        e.drag = handleAt(h, x);
        if (!e.drag) {  // nouvelle sélection, tracée à la souris
            e.drag = 3;
            e.anchor = xToSample(waveArea(h), x);
        }
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (e.drag) {
            size_t s = xToSample(waveArea(h), GET_X_LPARAM(lp));
            if (e.drag == 1) e.selStart = std::min(s, e.selEnd > kMinSelection ? e.selEnd - kMinSelection : 0);
            if (e.drag == 2) e.selEnd = std::min(e.raw.size(), std::max(s, e.selStart + kMinSelection));
            if (e.drag == 3) {
                e.selStart = std::min(e.anchor, s);
                e.selEnd = std::max(e.anchor, s);
            }
            InvalidateRect(h, nullptr, FALSE);
            updateSelectionLabel();
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (e.drag) {
            e.drag = 0;
            ReleaseCapture();
            if (e.selEnd < e.selStart + kMinSelection) {  // simple clic : on garde la sélection d'avant
                e.selStart = e.dragStart;
                e.selEnd = e.dragEnd;
            }
            InvalidateRect(h, nullptr, FALSE);
            updateSelectionLabel();
            if (e.selStart != e.dragStart || e.selEnd != e.dragEnd) {
                e.rawChanged = true;
                g_local.stopAll();
                processCapture();
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        e.drag = 0;
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void drawWave(const DRAWITEMSTRUCT* d) {
    Editor& e = p.editor;
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC dc = CreateCompatibleDC(d->hDC);
    HBITMAP canvas = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ oldCanvas = SelectObject(dc, canvas);
    RECT all = {0, 0, w, h};
    FillRect(dc, &all, theme::brush(theme::kBg));
    theme::roundRect(dc, all, float(S(8)), theme::kSurface);

    RECT r = waveArea(d->hwndItem);
    const int width = r.right - r.left;
    if (!e.raw.empty() && width > 0) {
        if (e.peaksWidth != width) {  // crête de chaque colonne de pixels, mise à l'échelle du plus fort
            e.peaks.assign(size_t(width), 0.0f);
            float top = 1e-4f;
            for (int x = 0; x < width; ++x) {
                size_t a = size_t(double(x) / width * e.raw.size()), b = size_t(double(x + 1) / width * e.raw.size());
                float m = 0;
                for (size_t i = a; i < std::min(b, e.raw.size()); ++i) m = std::max(m, std::fabs(e.raw[i]));
                e.peaks[size_t(x)] = m;
                top = std::max(top, m);
            }
            for (float& v : e.peaks) v /= top;
            e.peaksWidth = width;
        }
        int xs = sampleToX(r, e.selStart), xe = sampleToX(r, e.selEnd);
        RECT sel = {xs, S(2), xe, h - S(2)};
        FillRect(dc, &sel, theme::brush(theme::kAccentSoft));
        const int mid = (r.top + r.bottom) / 2, half = (r.bottom - r.top) / 2;
        HPEN inPen = CreatePen(PS_SOLID, 1, theme::kAccentLight), outPen = CreatePen(PS_SOLID, 1, theme::kBorder);
        HGDIOBJ oldPen = SelectObject(dc, outPen);
        for (int x = 0; x < width; ++x) {
            int px = r.left + x;
            SelectObject(dc, px >= xs && px <= xe ? inPen : outPen);
            int a = std::max(1, int(e.peaks[size_t(x)] * half));
            MoveToEx(dc, px, mid - a, nullptr);
            LineTo(dc, px, mid + a + 1);
        }
        // Les deux poignées.
        SelectObject(dc, oldPen);
        DeleteObject(inPen);
        DeleteObject(outPen);
        for (int x : {xs, xe}) {  // les deux poignées : un trait et une pastille ronde en haut
            RECT bar = {x - S(1), S(2), x + S(1), h - S(2)};
            theme::roundRect(dc, bar, float(S(1)), RGB(255, 255, 255));
            theme::circle(dc, float(x), float(S(7)), float(S(5)), RGB(255, 255, 255));
        }
    }
    BitBlt(d->hDC, rc.left, rc.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldCanvas);
    DeleteObject(canvas);
    DeleteDC(dc);
}

// Raccourci des « 10 dernières secondes » : réglé dans la page des boutons.
LRESULT CALLBACK replayKeyProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_SETFOCUS) unregisterHotkeys();  // sinon taper le raccourci actuel le déclencherait
    if (msg == WM_KILLFOCUS) {
        LRESULT result = DefSubclassProc(h, msg, wp, lp);
        WORD hk = WORD(SendMessageW(h, HKM_GETHOTKEY, 0, 0));
        p.replayHotkey = LOBYTE(hk) ? hk : 0;
        iniWrite(kSection, L"raccourci_replay", std::to_wstring(p.replayHotkey));
        if (!p.editor.open) registerHotkeys();
        updateStatus();
        return result;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------- textes de la page

void updateStatus() {
    std::wstring t;
    if (!p.notice.empty()) {
        t = p.notice;
    } else if (!p.hotkeyError.empty()) {
        t = L"Raccourci déjà pris par un autre logiciel : " + p.hotkeyError + L". Choisis-en un autre (clic droit → Modifier).";
    } else if (!voicepage::running()) {
        t = L"La voix est désactivée (onglet Voix) : les sons ne partent pas dans ton micro, toi seul les entends.";
    } else {
        t = L"● Les sons partent dans ton micro virtuel, avec ta voix : sur Discord, tes amis les entendent.";
    }
    setText(p.status, t);
    InvalidateRect(p.status, nullptr, TRUE);
}

void createFonts() {
    if (p.fontDpi == ui.dpi && p.emojiFont) return;
    if (p.emojiFont) DeleteObject(p.emojiFont);
    if (p.tinyFont) DeleteObject(p.tinyFont);
    p.emojiFont = CreateFontW(-MulDiv(13, int(ui.dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI Emoji");
    p.tinyFont = CreateFontW(-MulDiv(7, int(ui.dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
    p.fontDpi = ui.dpi;
}

// ---------------------------------------------------------------- dessin des boutons

void roundBox(HDC dc, const RECT& r, COLORREF fill, COLORREF border, float penWidth, bool dashed = false) {
    const float radius = float(S(10));
    if (fill != theme::kBg) theme::roundRect(dc, r, radius, fill);
    if (penWidth > 0) theme::roundFrame(dc, r, radius, border, penWidth, dashed);
}

void drawTile(const DRAWITEMSTRUCT* d) {
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC dc = CreateCompatibleDC(d->hDC);
    HBITMAP canvas = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ oldCanvas = SelectObject(dc, canvas);
    RECT all = {0, 0, w, h};
    FillRect(dc, &all, theme::brush(theme::kBg));
    SetBkMode(dc, TRANSPARENT);

    const bool pressed = d->itemState & ODS_SELECTED, hover = theme::isHover(d->hwndItem);
    const bool focused = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);  // au clavier seulement
    RECT box = {0, 0, w, h};
    if (int(d->CtlID) == IDC_ADD) {
        roundBox(dc, box, pressed ? theme::kSurfacePressed : hover ? theme::kSurface : theme::kBg,
                 focused ? theme::kAccentLight : hover ? theme::kFaint : theme::kBorder, 1.2f, true);
        HGDIOBJ old = SelectObject(dc, ui.bigFont);
        SetTextColor(dc, theme::kAccentLight);
        RECT plus = {0, S(10), w, S(10) + S(kImageDip)};
        DrawTextW(dc, L"+", 1, &plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, ui.smallFont);
        SetTextColor(dc, theme::kMuted);
        RECT label = {S(3), S(10) + S(kImageDip) + S(4), w - S(3), h - S(4)};
        DrawTextW(dc, L"Nouveau son", -1, &label, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
    } else if (int(d->CtlID) >= IDC_PENDING0 && int(d->CtlID) < IDC_PENDING0 + kMaxPending) {
        // Capture « à trier » : jaune, qui clignote doucement.
        const Pending& item = p.pending[size_t(int(d->CtlID) - IDC_PENDING0)];
        COLORREF fill = pressed ? theme::kSurfacePressed : p.blink || hover ? theme::kAmberSoft : theme::kSurface;
        roundBox(dc, box, fill, focused ? theme::kAccentLight : theme::kAmberLine, 1.2f, true);
        int px = S(kImageDip) * 4 / 5;
        if (!p.scissorsBitmap || p.scissorsPx != px) {
            if (p.scissorsBitmap) DeleteObject(p.scissorsBitmap);
            p.scissorsBitmap = soundimg::makeBitmap(p.scissors, px);
            p.scissorsPx = px;
        }
        soundimg::draw(dc, p.scissorsBitmap, (w - px) / 2, S(9), px);
        HGDIOBJ old = SelectObject(dc, ui.smallFont);
        SetTextColor(dc, theme::kAmber);
        RECT label = {S(3), S(9) + px + S(5), w - S(3), h - S(15)};
        DrawTextW(dc, L"À trier", -1, &label, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, p.tinyFont);
        SetTextColor(dc, theme::kFaint);
        RECT time = {S(2), h - S(16), w - S(2), h - S(3)};
        DrawTextW(dc, item.time.c_str(), -1, &time, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
    } else {
        Sound& s = p.sounds[size_t(int(d->CtlID) - IDC_TILE0)];
        bool playing = (p.lastPlaying >> (soundTag(s))) & 1;
        COLORREF fill = playing ? theme::kAccentSoft : pressed ? theme::kSurfacePressed : hover ? theme::kSurfaceHover : theme::kSurface;
        roundBox(dc, box, fill, focused ? theme::kAccentLight : theme::kAccentBright, playing || focused ? 1.5f : 0.0f);
        refreshBitmap(s, false);
        int px = s.bitmapPx;
        soundimg::draw(dc, s.bitmap, (w - px) / 2 + (pressed ? 1 : 0), S(7) + (pressed ? 1 : 0), px);

        HGDIOBJ old = SelectObject(dc, ui.smallFont);
        SetTextColor(dc, theme::kText);
        RECT name = {S(3), S(7) + px + S(3), w - S(3), h - S(15)};
        DrawTextW(dc, s.name.c_str(), -1, &name, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL | DT_NOPREFIX);
        if (LOBYTE(s.hotkey)) {
            SelectObject(dc, p.tinyFont);
            SetTextColor(dc, s.hotkeyOk ? theme::kFaint : theme::kDanger);
            RECT key = {S(2), h - S(16), w - S(2), h - S(3)};
            std::wstring k = hotkeyText(s.hotkey);
            DrawTextW(dc, k.c_str(), -1, &key, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        SelectObject(dc, old);
    }
    BitBlt(d->hDC, rc.left, rc.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldCanvas);
    DeleteObject(canvas);
    DeleteDC(dc);
}

void drawPreview(const DRAWITEMSTRUCT* d) {
    RECT rc = d->rcItem;
    FillRect(d->hDC, &rc, theme::brush(theme::kBg));
    roundBox(d->hDC, rc, theme::kSurface, theme::kBorder, 0.0f);
    if (p.editor.bitmapPx != S(kPreviewDip)) refreshEditorImage();
    int size = p.editor.bitmapPx;
    soundimg::draw(d->hDC, p.editor.bitmap, rc.left + (rc.right - rc.left - size) / 2,
                   rc.top + (rc.bottom - rc.top - size) / 2, size);
}

int tileIndex(HWND h) {
    for (int i = 0; i < int(p.sounds.size()); ++i)
        if (p.tiles[i] == h) return i;
    return -1;
}

int pendingIndex(HWND h) {
    for (int i = 0; i < int(p.pending.size()); ++i)
        if (p.pendingTiles[i] == h) return i;
    return -1;
}

}  // namespace

// ---------------------------------------------------------------- interface publique

SoundMixer* cableMixer() { return &g_cable; }

void reloadDevices() {
    closePlayer();
    openPlayer();
}

void create() {
    p.add = theme::button(L"Nouveau son", theme::Kind::kTile, IDC_ADD);
    for (int i = 0; i < kMaxSounds; ++i) p.tiles[i] = theme::button(L"", theme::Kind::kTile, IDC_TILE0 + i);
    p.empty = makeControl(L"STATIC", L"Ton soundboard est vide. Clique sur « + » pour capturer un passage d'une vidéo, "
                                     L"d'un jeu… (tout ce que joue ton PC).", SS_LEFT);
    p.volumeLabel = makeControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE);
    p.volume = makeSlider(0, 100, TBS_NOTICKS, IDC_VOLUME);
    SendMessageW(p.volume, TBM_SETPAGESIZE, 0, 10);
    p.stop = theme::button(L"■ Stop", theme::Kind::kButton, IDC_STOP);
    p.hear = theme::button(L"M'entendre aussi dans le casque", theme::Kind::kToggle, IDC_HEAR);
    p.status = makeControl(L"STATIC", L"", SS_LEFT);
    p.hint = makeControl(L"STATIC", L"Clic droit sur un son : modifier, déplacer, supprimer.\nCtrl+Maj+0 : arrêter tous les sons.",
                         SS_LEFT);

    p.title = makeControl(L"STATIC", L"Nouveau son", SS_LEFT | SS_CENTERIMAGE);
    p.nameLabel = makeControl(L"STATIC", L"Nom :", SS_LEFT | SS_CENTERIMAGE);
    p.name = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, ui.wnd,
                             reinterpret_cast<HMENU>(INT_PTR(IDC_NAME)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(p.name, EM_SETCUEBANNER, TRUE, LPARAM(L"ex. : le gars a fait prout"));
    SendMessageW(p.name, EM_LIMITTEXT, 60, 0);
    p.durationLabel = makeControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE);
    p.duration = makeSlider(1, kMaxDuration, TBS_AUTOTICKS, IDC_DURATION);
    p.capture = theme::button(L"● Capturer", theme::Kind::kButton, IDC_CAPTURE);
    p.listen = theme::button(L"▶ Écouter", theme::Kind::kButton, IDC_LISTEN);
    p.progress = theme::meter();
    SendMessageW(p.progress, PBM_SETRANGE32, 0, 100);
    p.message = makeControl(L"STATIC", L"", SS_LEFT);
    p.cleanLabel = makeControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE);
    p.clean = makeSlider(0, 100, TBS_NOTICKS, IDC_CLEAN);
    SendMessageW(p.clean, TBM_SETPAGESIZE, 0, 10);
    p.imageLabel = makeControl(L"STATIC", L"Image :", SS_LEFT);
    p.preview = makeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_PREVIEW);
    p.emoji = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, IDC_EMOJI);
    for (const wchar_t* e : kEmojis) SendMessageW(p.emoji, CB_ADDSTRING, 0, LPARAM(e));
    p.pick = theme::button(L"Choisir une image du PC…", theme::Kind::kButton, IDC_PICK);
    p.imageHint = makeControl(L"STATIC", L"", SS_LEFT);
    p.aiDraw = theme::button(L"✨ Dessiner", theme::Kind::kButton, IDC_AIDRAW);
    p.aiEmoji = theme::button(L"✨ Emoji", theme::Kind::kButton, IDC_AIEMOJI);
    p.hotkeyLabel = makeControl(L"STATIC", L"Raccourci :", SS_LEFT | SS_CENTERIMAGE);
    p.hotkey = theme::hotkeyBox(IDC_HOTKEY);
    // Pas de touche seule, ni Maj seule, ni Ctrl+Alt (= AltGr : @ # { [ | … ne s'écriraient plus).
    SendMessageW(p.hotkey, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S | HKCOMB_CA | HKCOMB_SCA,
                 MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_SHIFT, 0));
    p.noHotkey = theme::button(L"Aucun", theme::Kind::kButton, IDC_NOHOTKEY);
    p.soundVolLabel = makeControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE);
    p.soundVol = makeSlider(0, 200, TBS_NOTICKS, IDC_SOUNDVOL);
    SendMessageW(p.soundVol, TBM_SETPAGESIZE, 0, 10);
    p.save = theme::button(L"✓ Ajouter au soundboard", theme::Kind::kPrimary, IDC_SAVE);
    p.cancel = theme::button(L"Annuler", theme::Kind::kButton, IDC_CANCEL);

    // Replay : boutons « à trier », raccourci, forme du son dans l'éditeur.
    for (int i = 0; i < kMaxPending; ++i)
        p.pendingTiles[i] = theme::button(L"", theme::Kind::kTile, IDC_PENDING0 + i);
    p.replayLabel = makeControl(L"STATIC", L"Garder les 10 dernières s :", SS_LEFT | SS_CENTERIMAGE);
    p.replayKey = theme::hotkeyBox(IDC_REPLAYKEY);
    SendMessageW(p.replayKey, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S | HKCOMB_CA | HKCOMB_SCA,
                 MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_SHIFT, 0));
    SetWindowSubclass(p.replayKey, replayKeyProc, 1, 0);
    p.replayNone = theme::button(L"Aucun", theme::Kind::kButton, IDC_REPLAYNONE);
    theme::darkControl(p.name);
    theme::darkControl(p.emoji);
    p.wave = makeControl(L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, IDC_WAVE);
    SetWindowSubclass(p.wave, waveProc, 1, 0);
    p.selLabel = makeControl(L"STATIC", L"", SS_LEFT);
}

int layout(int top, bool shown) {
    p.shown = shown;
    createFonts();
    const int margin = 20, width = 400, inner = width - 2 * margin;
    const bool grid = shown && !p.editor.open, edit = shown && p.editor.open;
    int y = top + 6;

    // ---- les boutons
    const int count = int(p.sounds.size()), waiting = int(p.pending.size());
    for (int i = 0; i < kMaxSounds; ++i) show(p.tiles[i], grid && i < count);
    for (int i = 0; i < kMaxPending; ++i) show(p.pendingTiles[i], grid && i < waiting);
    show(p.add, grid && count < kMaxSounds);
    show(p.empty, grid && count == 0 && waiting == 0);
    for (HWND h : {p.volumeLabel, p.volume, p.stop, p.replayLabel, p.replayKey, p.replayNone, p.hear, p.status, p.hint})
        show(h, grid);
    const bool trimming = p.editor.pending >= 0, hasWave = !p.editor.raw.empty();
    HWND editors[] = {p.title,   p.nameLabel,  p.name,        p.listen,
                      p.message,   p.cleanLabel, p.clean,       p.imageLabel,    p.preview,  p.emoji,   p.pick,
                      p.imageHint, p.hotkeyLabel, p.hotkey,   p.noHotkey,      p.soundVolLabel, p.soundVol, p.save, p.cancel,
                      p.aiDraw,  p.aiEmoji};
    for (HWND h : editors) show(h, edit);
    for (HWND h : {p.durationLabel, p.duration, p.capture, p.progress}) show(h, edit && !trimming);  // découpage : pas de capture
    for (HWND h : {p.wave, p.selLabel}) show(h, edit && hasWave);

    if (!p.editor.open) {
        // Les sons, puis les captures « à trier », puis « + ».
        int tiles = count + waiting + (count < kMaxSounds ? 1 : 0);
        for (int i = 0; i < tiles; ++i) {
            HWND h = i < count ? p.tiles[i] : i < count + waiting ? p.pendingTiles[i - count] : p.add;
            place(h, margin + (i % kColumns) * (kTileW + kGap), y + (i / kColumns) * (kTileH + kGap), kTileW, kTileH, ui.smallFont);
            InvalidateRect(h, nullptr, FALSE);
        }
        if (count == 0 && waiting == 0) place(p.empty, margin + kTileW + 14, y + 30, inner - kTileW - 14, kTileH - 30, ui.smallFont);
        y += ((tiles + kColumns - 1) / kColumns) * (kTileH + kGap) + 6;
        place(p.volumeLabel, margin, y, 104, 30, ui.smallFont);
        place(p.volume, margin + 104, y, inner - 104 - 90, 30, ui.font);
        place(p.stop, width - margin - 84, y, 84, 30, ui.font);
        y += 38;
        place(p.replayLabel, margin, y, 150, 26, ui.smallFont);
        place(p.replayKey, margin + 150, y, inner - 150 - 90, 26, ui.font);
        place(p.replayNone, width - margin - 80, y, 80, 26, ui.smallFont);
        y += 34;
        place(p.hear, margin, y, inner, 22, ui.smallFont);
        y += 30;
        place(p.status, margin, y, inner, 36, ui.smallFont);
        y += 40;
        place(p.hint, margin, y, inner, 32, ui.smallFont);
        y += 38;
        if (grid) updateStatus();
        return y;
    }

    // ---- l'éditeur
    place(p.title, margin, y, inner, 28, ui.font);
    y += 34;
    place(p.nameLabel, margin, y, 76, 26, ui.smallFont);
    theme::placeInput(p.name, margin + 76, y, inner - 76, 28, ui.font);
    y += 34;
    if (!trimming) {
        place(p.durationLabel, margin, y, 76, 30, ui.smallFont);
        place(p.duration, margin + 76, y, inner - 76, 30, ui.font);
        y += 36;
        place(p.capture, margin, y, 175, 32, ui.font);
        place(p.listen, width - margin - 175, y, 175, 32, ui.font);
        y += 40;
        place(p.progress, margin, y, inner, 6, ui.smallFont);
        y += 12;
    }
    if (hasWave) {  // la forme du son, avec le passage gardé entre les deux poignées
        place(p.wave, margin, y, inner, 64, ui.smallFont);
        y += 68;
        place(p.selLabel, margin, y, inner - (trimming ? 185 : 0), 20, ui.smallFont);
        if (trimming) place(p.listen, width - margin - 175, y - 2, 175, 30, ui.font);
        y += trimming ? 34 : 24;
        InvalidateRect(p.wave, nullptr, FALSE);
    } else if (trimming) {
        place(p.listen, width - margin - 175, y, 175, 32, ui.font);
        y += 40;
    }
    place(p.message, margin, y, inner, 36, ui.smallFont);
    y += 40;
    place(p.cleanLabel, margin, y, 120, 30, ui.smallFont);
    place(p.clean, margin + 120, y, inner - 120, 30, ui.font);
    y += 38;
    place(p.imageLabel, margin, y + 4, 76, 20, ui.smallFont);
    place(p.preview, margin + 76, y, kPreviewDip, kPreviewDip, ui.smallFont);
    const int right = margin + 76 + kPreviewDip + 10, rightW = width - margin - right;
    place(p.emoji, right, y, rightW, 300, p.emojiFont);
    place(p.pick, right, y + 33, rightW, 26, ui.smallFont);
    const int half = (rightW - 6) / 2;
    place(p.aiDraw, right, y + 63, half, 26, ui.smallFont);
    place(p.aiEmoji, right + half + 6, y + 63, rightW - half - 6, 26, ui.smallFont);
    y += 95;
    place(p.imageHint, margin + 76, y, inner - 76, 32, ui.smallFont);
    y += 14;
    y += 28;
    place(p.hotkeyLabel, margin, y, 76, 26, ui.smallFont);
    place(p.hotkey, margin + 76, y, inner - 76 - 90, 26, ui.font);
    place(p.noHotkey, width - margin - 80, y, 80, 26, ui.smallFont);
    y += 34;
    place(p.soundVolLabel, margin, y, 120, 30, ui.smallFont);
    place(p.soundVol, margin + 120, y, inner - 120, 30, ui.font);
    y += 40;
    place(p.save, margin, y, 210, 32, ui.font);
    place(p.cancel, width - margin - 120, y, 120, 32, ui.font);
    y += 44;
    if (edit) refreshEditorImage();
    return y;
}

bool onCommand(WPARAM wp, LPARAM) {
    const int id = LOWORD(wp), code = HIWORD(wp);
    if (id >= IDC_TILE0 && id < IDC_TILE0 + int(p.sounds.size())) {
        playSound(p.sounds[size_t(id - IDC_TILE0)]);
        return true;
    }
    if (id >= IDC_PENDING0 && id < IDC_PENDING0 + int(p.pending.size())) {
        openEditor(-1, id - IDC_PENDING0);
        return true;
    }
    switch (id) {
    case IDC_ADD: openEditor(-1); return true;
    case IDC_REPLAYNONE:
        SendMessageW(p.replayKey, HKM_SETHOTKEY, 0, 0);
        p.replayHotkey = 0;
        iniWrite(kSection, L"raccourci_replay", L"0");
        registerHotkeys();
        updateStatus();
        return true;
    case IDC_STOP: stopAllSounds(); return true;
    case IDC_HEAR:
        iniWrite(kSection, L"casque", hearMyself() ? L"1" : L"0");
        if (!hearMyself()) g_local.stopAll();
        return true;
    case IDC_CAPTURE:
        if (p.rec.state == Recorder::kIdle) startCapture();
        else if (p.rec.state == Recorder::kWaiting) cancelCapture();
        else finishCapture();
        return true;
    case IDC_LISTEN:
        if (p.editor.samples && openPlayer()) {
            g_local.stopAll();
            p.editor.previewed = p.editor.samples->data();
            g_local.play(p.editor.samples->data(), int(p.editor.samples->size()), float(sliderPos(p.soundVol)) / 100.0f,
                         kPreviewTag);
        }
        return true;
    case IDC_EMOJI:
        if (code == CBN_SELCHANGE) {
            int sel = int(SendMessageW(p.emoji, CB_GETCURSEL, 0, 0));
            if (sel >= 0) imageFromEmoji(kEmojis[sel]);
        } else if (code == CBN_EDITCHANGE) {
            imageFromEmoji(text(p.emoji));
        }
        return true;
    case IDC_PICK: {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = ui.wnd;
        ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.ico;*.tif;*.tiff;*.heic\0Tous les fichiers\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Image du bouton";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) {
            soundimg::Image image;
            if (soundimg::loadFile(file, image)) {
                p.editor.image = std::move(image);
                SetWindowTextW(p.emoji, L"");  // l'image vient d'un fichier, plus d'un emoji
                refreshEditorImage();
            } else {
                setMessage(L"Cette image ne peut pas être lue. Essaie un PNG ou un JPG.", true);
            }
        }
        return true;
    }
    case IDC_NOHOTKEY:
        SendMessageW(p.hotkey, HKM_SETHOTKEY, 0, 0);
        return true;
    case IDC_NAME:
        if (code == EN_CHANGE && p.editor.open) updateEditorButtons();  // les boutons ✨ demandent un nom
        return true;
    case IDC_AIDRAW:
    case IDC_AIEMOJI:
        if (claude::request(id == IDC_AIDRAW ? claude::Kind::kDrawing : claude::Kind::kEmoji, trim(text(p.name)))) {
            p.editor.askedClaude = true;
            p.editor.claudeError.clear();
        }
        updateEditorButtons();
        return true;
    case IDC_SAVE: saveEditor(); return true;
    case IDC_CANCEL: closeEditor(); return true;
    }
    return false;
}

bool onScroll(HWND slider, int code) {
    if (slider == p.volume) {
        applyVolume(true);
        return true;
    }
    if (slider == p.duration) {
        iniWrite(kSection, L"duree", std::to_wstring(sliderPos(p.duration)));
        updateEditorButtons();
        return true;
    }
    if (slider == p.soundVol) {
        updateEditorButtons();
        return true;
    }
    if (slider == p.clean) {
        updateEditorButtons();
        // On renettoie quand le curseur est lâché (pas à chaque pixel pendant qu'on le fait glisser).
        if (code != TB_THUMBTRACK) {
            iniWrite(kSection, L"nettoyage", std::to_wstring(sliderPos(p.clean)));
            if (!p.editor.raw.empty()) processCapture();
        }
        return true;
    }
    return false;
}

bool onDrawItem(const DRAWITEMSTRUCT* item) {
    int id = int(item->CtlID);
    if (id == IDC_PREVIEW) {
        drawPreview(item);
        return true;
    }
    if (id == IDC_WAVE) {
        drawWave(item);
        return true;
    }
    if (id == IDC_ADD || (id >= IDC_TILE0 && id < IDC_TILE0 + int(p.sounds.size())) ||
        (id >= IDC_PENDING0 && id < IDC_PENDING0 + int(p.pending.size()))) {
        drawTile(item);
        return true;
    }
    return false;
}

bool onContextMenu(HWND control, int x, int y) {
    int index = tileIndex(control), waiting = pendingIndex(control);
    if (index < 0 && waiting < 0) return false;
    if (x == -1 && y == -1) {  // touche « menu » du clavier
        RECT r;
        GetWindowRect(control, &r);
        x = r.left + S(10);
        y = r.top + S(10);
    }
    if (waiting >= 0) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuEdit, L"Découper…");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuDelete, L"Supprimer");
        int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, ui.wnd, nullptr);
        DestroyMenu(menu);
        if (choice == kMenuEdit) openEditor(-1, waiting);
        if (choice == kMenuDelete) deletePending(waiting);
        return true;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuPlay, L"Jouer");
    AppendMenuW(menu, MF_STRING, kMenuEdit, L"Modifier…");
    AppendMenuW(menu, MF_STRING | (index == 0 ? MF_GRAYED : 0), kMenuLeft, L"Déplacer vers la gauche");
    AppendMenuW(menu, MF_STRING | (index + 1 == int(p.sounds.size()) ? MF_GRAYED : 0), kMenuRight, L"Déplacer vers la droite");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuDelete, L"Supprimer");
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, ui.wnd, nullptr);
    DestroyMenu(menu);
    switch (choice) {
    case kMenuPlay: playSound(p.sounds[size_t(index)]); break;
    case kMenuEdit: openEditor(index); break;
    case kMenuLeft:
    case kMenuRight: {
        int other = index + (choice == kMenuLeft ? -1 : 1);
        std::swap(p.sounds[size_t(index)], p.sounds[size_t(other)]);
        saveIndex();
        InvalidateRect(p.tiles[index], nullptr, FALSE);
        InvalidateRect(p.tiles[other], nullptr, FALSE);
        break;
    }
    case kMenuDelete: deleteSound(index); break;
    }
    return true;
}

bool onHotkey(int id) {
    if (id == kStopHotkeyId) {
        stopAllSounds();
        return true;
    }
    if (id == kReplayHotkeyId) {
        captureReplay();
        return true;
    }
    if (Sound* s = findUid(id - kHotkeyBase)) {
        playSound(*s);
        return true;
    }
    return false;
}

bool isStatus(HWND h, bool& error) {
    if (h == p.status) {
        error = !p.notice.empty() ? p.noticeError : !p.hotkeyError.empty() || !voicepage::running();
        return true;
    }
    if (h == p.selLabel) {
        error = false;
        return true;
    }
    if (h == p.message) {
        error = p.editor.messageError;
        return true;
    }
    if (h == p.hint || h == p.imageHint || h == p.empty || h == p.nameLabel || h == p.durationLabel || h == p.cleanLabel ||
        h == p.imageLabel || h == p.hotkeyLabel || h == p.soundVolLabel || h == p.volumeLabel || h == p.replayLabel) {
        error = false;
        return true;
    }
    return false;
}

void onTimer() {
    tickRecorder();
    // Boutons « en cours de lecture » : surlignés tant que le son joue.
    uint64_t playing = g_cable.playing() | g_local.playing();
    if (playing != p.lastPlaying) {
        uint64_t changed = playing ^ p.lastPlaying;
        p.lastPlaying = playing;
        for (int i = 0; i < int(p.sounds.size()); ++i)
            if ((changed >> (soundTag(p.sounds[size_t(i)]))) & 1) InvalidateRect(p.tiles[i], nullptr, FALSE);
    }
    bool voice = voicepage::running();
    if (voice != p.lastVoice) {
        p.lastVoice = voice;
        if (p.shown && !p.editor.open) updateStatus();
    }
    if (p.player.lost) closePlayer();  // casque débranché : on rouvrira au prochain son

    // ✨ Réponse de Claude : on l'applique si l'éditeur qui l'a demandée est toujours ouvert.
    static claude::Status lastStatus = claude::Status::kChecking;
    static bool lastBusy = false;
    claude::Result answer;
    if (claude::takeResult(answer) && p.editor.open && p.editor.askedClaude) {
        p.editor.askedClaude = false;
        soundimg::Image image;
        if (!answer.ok) {
            p.editor.claudeError = answer.error;
        } else if (answer.kind == claude::Kind::kEmoji) {
            SetWindowTextW(p.emoji, answer.emoji.c_str());
            imageFromEmoji(answer.emoji);
        } else if (soundimg::renderSvg(answer.svg, image)) {
            p.editor.image = std::move(image);
            SetWindowTextW(p.emoji, L"");  // l'image est un dessin, plus un emoji
            refreshEditorImage();
        } else {
            p.editor.claudeError = L"Le dessin de Claude n'a pas pu être affiché. Réessaie.";
        }
        updateEditorButtons();
    }
    if (p.editor.open && (claude::status() != lastStatus || claude::busy() != lastBusy)) updateEditorButtons();
    lastStatus = claude::status();
    lastBusy = claude::busy();

    const DWORD now = GetTickCount();
    if (now - p.replayCheck > 1000) {  // la sortie par défaut a changé ? le replay la suit
        p.replayCheck = now;
        followDefaultOutput();
    }
    if (!p.pending.empty() && now - p.blinkTick > 600) {  // les captures « à trier » clignotent
        p.blinkTick = now;
        p.blink = !p.blink;
        if (p.shown && !p.editor.open)
            for (int i = 0; i < int(p.pending.size()); ++i) InvalidateRect(p.pendingTiles[i], nullptr, FALSE);
    }
    if (!p.notice.empty() && now - p.noticeTick > 7000) {
        p.notice.clear();
        if (p.shown && !p.editor.open) updateStatus();
    }
}

void startup() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    p.folder = exe;
    p.folder = p.folder.substr(0, p.folder.find_last_of(L"\\/") + 1) + L"Sons\\";

    soundimg::init();
    ma_backend backends[] = {ma_backend_wasapi};
    ma_context_config config = ma_context_config_init();
    p.contextReady = ma_context_init(backends, 1, &config, &p.context) == MA_SUCCESS;

    setSliderPos(p.volume, std::clamp(iniReadInt(kSection, L"volume", 80), 0, 100));
    applyVolume(false);
    setSliderPos(p.duration, std::clamp(iniReadInt(kSection, L"duree", 3), 1, kMaxDuration));
    SendMessageW(p.hear, BM_SETCHECK, iniReadInt(kSection, L"casque", 1) ? BST_CHECKED : BST_UNCHECKED, 0);

    std::wstring replayKey = iniRead(kSection, L"raccourci_replay");
    p.replayHotkey = replayKey.empty() ? kDefaultReplayHotkey : WORD(_wtoi(replayKey.c_str()));
    SendMessageW(p.replayKey, HKM_SETHOTKEY, p.replayHotkey, 0);
    makeBips();
    soundimg::renderText(L"✂️", p.scissors);
    claude::check();  // en arrière-plan : Claude Code est-il installé et connecté ?

    loadAll();
    loadPending();
    registerHotkeys();
    openPlayer();
    followDefaultOutput();  // le replay écoute dès le lancement
    p.replayCheck = GetTickCount();
    p.lastVoice = voicepage::running();
    relayout();
}

void shutdown() {
    closeRecorder();
    unregisterHotkeys();
    claude::shutdown();
    p.replay.close();
    closePlayer();
    if (p.scissorsBitmap) DeleteObject(p.scissorsBitmap);
    for (Sound& s : p.sounds)
        if (s.bitmap) DeleteObject(s.bitmap);
    if (p.editor.bitmap) DeleteObject(p.editor.bitmap);
    if (p.contextReady) ma_context_uninit(&p.context);
    p.contextReady = false;
    soundimg::shutdown();
}

}  // namespace soundpage
