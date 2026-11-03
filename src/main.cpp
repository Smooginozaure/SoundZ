// SoundZ — trois onglets :
//  • Musique : transpose toute la musique de Windows de −4 à +4 demi-tons (sas VB-CABLE -> ta sortie habituelle).
//  • Voix : autotune / vocodeur sur le micro, envoyé aux applis par CABLE-A.
//  • Soundboard : sons capturés sur le PC, envoyés dans le micro (CABLE-A) d'un clic ou d'un raccourci.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "cables.h"
#include "default_output.h"
#include "engine.h"
#include "ui.h"
#include "util.h"
#include "shell_integration.h"
#include "sound_image.h"
#include "sound_page.h"
#include "voice_page.h"

int runSelfTest(const std::wstring& reportPath);                                   // selftest.cpp
int runEngineTest(const std::wstring& reportPath, const std::wstring& testOutput);  // selftest.cpp
int runVoiceTest(const std::wstring& reportPath);                                  // selftest.cpp
int runSoundTest(const std::wstring& reportPath);
int runClaudeTest(const std::wstring& reportPath, const std::wstring& name);  // selftest.cpp                                  // selftest.cpp
int runShellTest(const std::wstring& reportPath);                                  // selftest.cpp
int runCableTest(const std::wstring& reportPath);                                  // selftest.cpp

Ui ui;

namespace {

constexpr wchar_t kClassName[] = L"SoundZWindow";
constexpr wchar_t kTitle[] = L"SoundZ";
constexpr UINT WM_APP_DEVICE_LOST = WM_APP + 1;
constexpr UINT_PTR kStatusTimer = 1;
constexpr UINT_PTR kVolumeTimer = 2;
constexpr UINT_PTR kVoiceTimer = 3;
constexpr UINT_PTR kTaskbarTimer = 4;   // bouton de la barre des tâches refait 1,5 s après l'apparition…
constexpr UINT_PTR kTaskbarTimer2 = 5;  // … et encore à 5 s (Windows le place parfois en retard)
constexpr int kTickCount = PitchEngine::kMaxSemitones - PitchEngine::kMinSemitones + 1;
constexpr int kWidth = 400;   // largeur de la fenêtre (DIP)
constexpr int kPageTop = 46;  // sous les onglets

enum ControlId : int {
    IDC_TAB0 = 90,  // … IDC_TAB0 + 2 : les trois onglets
    IDC_OPTIONS = 95,  // ⚙ : raccourcis, lancement avec Windows
    IDC_FEEDBACK = 96, // 🪳 : signaler un bug, proposer une fonctionnalité (sur GitHub)
    IDC_RESET = 100,
    IDC_TOGGLE,
    IDC_SETTINGS,
    IDC_SAS,
    IDC_OUT,
    IDC_DEV_OUT = 110,  // page « Appareils »
    IDC_DEV_MIC,
    IDC_DEV_BACK,
    IDC_DEV_CABLE,  // un seul câble virtuel : voix ou musique
    IDC_DEV_OPEN,  // ouvre la page « Appareils » (comme ⚙ → Appareils… ; sert aussi aux tests)
    IDC_QUIT,      // quitte vraiment SoundZ (même si la croix réduit la fenêtre)
};

constexpr int kDevicesPage = 3;  // page ouverte depuis ⚙ (pas un onglet)
bool g_quitting = false;         // « Quitter SoundZ » : la fermeture n'est plus une réduction

// Option « Réduire au lieu de fermer » : la croix réduit SoundZ dans la barre des tâches (tout continue).
bool minimizeOnClose() { return iniReadInt(L"fenetre", L"reduire", 0) != 0; }

struct PitchPage {
    HWND label = nullptr;
    HWND slider = nullptr;
    HWND ticks[kTickCount] = {};
    HWND reset = nullptr;
    HWND toggle = nullptr;
    HWND status = nullptr;
    HWND settings = nullptr;
    HWND sasLabel = nullptr;
    HWND sasCombo = nullptr;
    HWND outLabel = nullptr;
    HWND outCombo = nullptr;
    bool settingsShown = false;

    PitchEngine engine;
    std::vector<OutputDevice> devices;
    int sasIndex = -1;
    cables::Assignment cables;  // qui a quel câble virtuel (musique / voix)
    int outIndex = -1;

    std::wstring error;
    uint64_t lastCaptured = 0;
    DWORD lastAudioTick = 0;
};

HWND g_tabs[3] = {};  // onglets (boutons « pastille » du thème)
HWND g_options = nullptr;  // ⚙
HWND g_feedback = nullptr;  // 🪳
HWND g_tooltips = nullptr;  // bulles d'aide (🪳, ⚙)
soundimg::Image g_bugImage;
HBITMAP g_bugBitmap = nullptr;
int g_bugPx = 0;

// Page « Appareils » : la sortie (là où l'on écoute) et le micro, en automatique ou choisis un par un.
struct DevicesPage {
    HWND title = nullptr, intro = nullptr;
    HWND outLabel = nullptr, outCombo = nullptr, outNow = nullptr;
    HWND micLabel = nullptr, micCombo = nullptr, micNow = nullptr;
    HWND cableLabel = nullptr, cableCombo = nullptr, cableNow = nullptr;
    HWND back = nullptr;
    std::vector<std::wstring> outIds, micIds;  // identifiants des appareils de chaque liste ("" = automatique)
};
DevicesPage d;
int g_lastTab = 0;  // onglet où revenir en quittant la page « Appareils »

void showPage(int page);

// Menu ⚙ : appareils, raccourcis et lancement avec Windows (cochés selon ce qui existe déjà).
void showOptions() {
    enum { kStartMenu = 1, kDesktop, kAutostart, kFolder, kDevices, kMinimize, kQuit };
    const struct { int id; shellint::Item item; const wchar_t* text; } items[] = {
        {kStartMenu, shellint::Item::kStartMenu, L"Raccourci dans le menu Démarrer"},
        {kDesktop, shellint::Item::kDesktop, L"Icône sur le Bureau"},
        {kAutostart, shellint::Item::kAutostart, L"Lancer SoundZ avec Windows"},
    };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kDevices, L"Appareils (sortie et micro)…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (const auto& it : items)
        AppendMenuW(menu, MF_STRING | (shellint::enabled(it.item) ? MF_CHECKED : 0), it.id, it.text);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (minimizeOnClose() ? MF_CHECKED : 0), kMinimize, L"Réduire au lieu de fermer");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kFolder, L"Ouvrir le dossier de SoundZ");
    AppendMenuW(menu, MF_STRING, kQuit, L"Quitter SoundZ");
    RECT r;
    GetWindowRect(g_options, &r);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom + S(4), 0, ui.wnd, nullptr);
    DestroyMenu(menu);
    for (const auto& it : items) {
        if (choice != it.id) continue;
        if (!shellint::set(it.item, !shellint::enabled(it.item)))
            MessageBoxW(ui.wnd, L"Windows a refusé la modification.", L"SoundZ", MB_OK | MB_ICONWARNING);
    }
    if (choice == kDevices) showPage(kDevicesPage);
    if (choice == kMinimize) iniWrite(L"fenetre", L"reduire", minimizeOnClose() ? L"0" : L"1");
    if (choice == kQuit) {
        g_quitting = true;
        PostMessageW(ui.wnd, WM_CLOSE, 0, 0);
    }
    if (choice == kFolder) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring args = L"/select,\"" + std::wstring(exe) + L"\"";
        ShellExecuteW(ui.wnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
}
int g_page = 0;  // 0 = Musique, 1 = Voix, 2 = Soundboard
PitchPage g;

// ---------------------------------------------------------------- choix des sorties

int findDevice(const std::wstring& preferred, std::initializer_list<const wchar_t*> names, int exclude) {
    for (int i = 0; i < int(g.devices.size()); ++i) {
        if (i != exclude && !preferred.empty() && sameText(g.devices[size_t(i)].endpoint, preferred)) return i;
    }
    for (const wchar_t* name : names) {
        for (int i = 0; i < int(g.devices.size()); ++i) {
            if (i != exclude && contains(g.devices[size_t(i)].name, name)) return i;
        }
    }
    return -1;
}

void fillCombo(HWND combo, int selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& device : g.devices) SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(device.name.c_str()));
    SendMessageW(combo, CB_SETCURSEL, WPARAM(selected), 0);
}

// La vraie sortie (là où l'on écoute), pour n'importe quel PC :
//  1. celle choisie dans les réglages ;
//  2. sinon la sortie par défaut de Windows, si ce n'est pas un câble virtuel (et on la retient) ;
//  3. sinon la dernière vraie sortie par défaut vue (par exemple juste après l'installation de VB-CABLE, qui
//     se met lui-même par défaut) ;
//  4. sinon un vrai casque ou des enceintes (pas un écran, pas une sortie ligne), sinon n'importe quelle
//     vraie sortie.
int findListenOutput() {
    auto index = [](const std::wstring& endpoint) {
        for (int i = 0; i < int(g.devices.size()); ++i)
            if (!endpoint.empty() && sameText(g.devices[size_t(i)].endpoint, endpoint)) return i;
        return -1;
    };
    int i = index(iniRead(L"vocoder", L"sortie"));
    if (i < 0) {
        i = index(getDefaultOutput());
        if (i >= 0 && isVirtualDevice(g.devices[size_t(i)].name)) i = -1;
        if (i >= 0) iniWrite(L"appareils", L"ecoute", g.devices[size_t(i)].endpoint);
    }
    if (i < 0) i = index(ui.listenOutput);
    if (i < 0) i = index(iniRead(L"appareils", L"ecoute"));
    for (int k = 0; k < int(g.devices.size()) && i < 0; ++k)
        if (!isVirtualDevice(g.devices[size_t(k)].name) && isPersonalDevice(g.devices[size_t(k)].endpoint)) i = k;
    for (int k = 0; k < int(g.devices.size()) && i < 0; ++k)
        if (!isVirtualDevice(g.devices[size_t(k)].name)) i = k;
    return i;
}

void refreshDevices() {
    g.devices = g.engine.outputs();
    g.outIndex = findListenOutput();
    // Le sas de la musique : un câble virtuel que la voix n'utilise pas (voir cables.h).
    std::vector<std::wstring> names;
    for (const auto& device : g.devices) names.push_back(device.name);
    auto indexOf = [](const std::wstring& endpoint) {
        for (int i = 0; i < int(g.devices.size()); ++i)
            if (!endpoint.empty() && sameText(g.devices[size_t(i)].endpoint, endpoint)) return i;
        return -1;
    };
    g.cables = cables::assign(names, indexOf(iniRead(L"vocoder", L"sas")), indexOf(iniRead(L"voix", L"cable")),
                              iniRead(L"appareils", L"cable_unique") == L"musique");
    g.sasIndex = g.cables.music;
    if (g.outIndex >= 0) {
        ui.listenOutput = g.devices[size_t(g.outIndex)].endpoint;
        ui.listenName = shortDeviceName(g.devices[size_t(g.outIndex)].name);
    }
    fillCombo(g.sasCombo, g.sasIndex);
    fillCombo(g.outCombo, g.outIndex);
}

// ---------------------------------------------------------------- marche / arrêt de la musique

// Ce que l'utilitaire change dans Windows pendant qu'il tourne, et qu'il remet en place en s'arrêtant.
// C'est aussi écrit dans SoundZ.ini (section [restaurer]) pour pouvoir réparer après un plantage.
struct Changes {
    bool active = false;
    std::wstring sas;              // sortie qui sert de sas
    std::wstring previousDefault;  // sortie par défaut d'avant
    int effects = -1;              // état d'origine des améliorations audio du sas (-1, 0, 1)
    float volume = 1;              // volume d'origine du sas
    bool muted = false;
};
Changes g_changes;
EndpointVolume g_sasVolume;

void saveChanges(const Changes& c) {
    iniWrite(L"restaurer", L"sas", c.sas);
    iniWrite(L"restaurer", L"sortie_par_defaut", c.previousDefault);
    iniWrite(L"restaurer", L"effets", std::to_wstring(c.effects));
    iniWrite(L"restaurer", L"volume", std::to_wstring(c.volume));
    iniWrite(L"restaurer", L"muet", c.muted ? L"1" : L"0");
}

// Remet Windows comme avant : sortie par défaut, effets et volume du sas.
void undoChanges(const Changes& c) {
    if (!c.previousDefault.empty() && sameText(getDefaultOutput(), c.sas)) setDefaultOutput(c.previousDefault);
    EndpointVolume volume;
    if (volume.open(c.sas)) volume.set(c.volume, c.muted);
    if (c.effects != 1 && getEffectsDisabled(c.sas) == 1) setEffectsDisabled(c.sas, false);
}

// Au lancement : si la dernière session s'est mal terminée, on répare d'abord.
void repairAfterCrash() {
    Changes c;
    c.sas = iniRead(L"restaurer", L"sas");
    if (c.sas.empty()) return;
    c.previousDefault = iniRead(L"restaurer", L"sortie_par_defaut");
    std::wstring effects = iniRead(L"restaurer", L"effets");
    std::wstring volume = iniRead(L"restaurer", L"volume");
    c.effects = effects.empty() ? -1 : _wtoi(effects.c_str());
    c.volume = volume.empty() ? 1.0f : float(_wtof(volume.c_str()));
    c.muted = iniRead(L"restaurer", L"muet") == L"1";
    undoChanges(c);
    iniDeleteSection(L"restaurer");
}

void stopPitch() {
    if (!g_changes.active) {
        g.engine.stop();
        return;
    }
    // D'abord rendre la vraie sortie à Windows (les applis y retournent directement), puis couper le traitement.
    if (sameText(getDefaultOutput(), g_changes.sas)) setDefaultOutput(g_changes.previousDefault);
    g.engine.stop();
    g_sasVolume.close();
    undoChanges(g_changes);
    iniDeleteSection(L"restaurer");
    g_changes = Changes();
}

void startPitch() {
    stopPitch();
    g.error.clear();
    if (g.outIndex < 0) {
        g.error = L"Aucune sortie audio trouvée (casque, enceintes). Branche-la, ou choisis-la dans les réglages.";
        return;
    }
    if (g.sasIndex < 0 || g.sasIndex == g.outIndex) {
        g.error = g.cables.count >= 1
                      ? L"Un seul câble virtuel, et il sert à ta voix. Pour transposer aussi la musique, installe un 2e câble "
                        L"(Hi-Fi Cable, gratuit), ou donne-le à la musique : ⚙ → Appareils."
                      : L"Aucun câble virtuel : installe VB-CABLE (gratuit, vb-audio.com/Cable), redémarre, puis clique sur Activer.";
        return;
    }
    const OutputDevice& sas = g.devices[size_t(g.sasIndex)];
    const OutputDevice& out = g.devices[size_t(g.outIndex)];

    // 1. On note l'état d'origine (dans le .ini aussi, au cas où).
    Changes c;
    c.active = true;
    c.sas = sas.endpoint;
    std::wstring current = getDefaultOutput();
    c.previousDefault = (current.empty() || sameText(current, sas.endpoint)) ? out.endpoint : current;
    c.effects = getEffectsDisabled(sas.endpoint);
    g_sasVolume.open(sas.endpoint);
    g_sasVolume.get(c.volume, c.muted);
    saveChanges(c);
    g_changes = c;

    // 2. Le sas doit transmettre le son tel quel : pas d'effets, volume à 100 %.
    if (c.effects != 1 && setEffectsDisabled(sas.endpoint, true)) Sleep(200);  // Windows relance la sortie
    g_sasVolume.set(1.0f, false);
    g.engine.setGain(1.0f);

    // 3. Traitement, puis bascule de la sortie Windows sur le sas.
    std::wstring error;
    if (!g.engine.start(sas, out, error)) {
        stopPitch();
        g.error = error;
        return;
    }
    if (!setDefaultOutput(sas.endpoint)) {
        g.error = L"Windows refuse de changer la sortie par défaut. Choisis « " + sas.name +
                  L" » à la main dans les paramètres Son.";
    }
    g.lastCaptured = 0;
    g.lastAudioTick = 0;
}

// Les touches de volume règlent maintenant le sas : on applique ce volume au son transposé.
void followVolume() {
    if (g.engine.running() && g_sasVolume.isOpen()) g.engine.setGain(g_sasVolume.gain());
}

// ---------------------------------------------------------------- affichage

std::wstring semitoneText(int value) {
    if (value == 0) return L"0  (son d'origine)";
    std::wstring sign = value > 0 ? L"+" : L"−";
    int magnitude = value > 0 ? value : -value;
    return sign + std::to_wstring(magnitude) + (magnitude == 1 ? L" demi-ton" : L" demi-tons");
}

void applySemitones(int value) {
    value = std::clamp(value, PitchEngine::kMinSemitones, PitchEngine::kMaxSemitones);
    if (sliderPos(g.slider) != value) setSliderPos(g.slider, value);
    g.engine.setSemitones(value);
    setText(g.label, semitoneText(value));
}

void updateStatus() {
    std::wstring text;
    if (!g.error.empty()) {
        text = g.error;
    } else if (g.engine.running()) {
        uint64_t captured = g.engine.capturedFrames();
        if (captured != g.lastCaptured) {
            g.lastCaptured = captured;
            g.lastAudioTick = GetTickCount();
        }
        bool receiving = g.lastAudioTick != 0 && GetTickCount() - g.lastAudioTick < 1500;
        const std::wstring& outName = g.devices[size_t(g.outIndex)].name;
        text = receiving ? L"● Actif — le son transposé sort sur " + outName + L"."
                         : L"● Actif — en attente de son. Lance ta musique.";
    } else {
        text = L"En pause — le son sort normalement, sans transposition.";
    }
    setText(g.status, text);
}

void updateUi() {
    setText(g.toggle, g.engine.running() ? L"Désactiver" : L"Activer");
    setText(g.settings, g.settingsShown ? L"Réglages ▴" : L"Réglages ▾");
    updateStatus();
}

void createFonts() {
    for (HFONT* f : {&ui.font, &ui.bigFont, &ui.smallFont}) {
        if (*f) DeleteObject(*f);
    }
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, ui.dpi);
    LOGFONTW base = metrics.lfMessageFont;

    LOGFONTW normal = base;
    normal.lfHeight = -MulDiv(10, int(ui.dpi), 72);
    ui.font = CreateFontIndirectW(&normal);

    LOGFONTW big = base;
    big.lfHeight = -MulDiv(18, int(ui.dpi), 72);
    big.lfWeight = FW_SEMIBOLD;
    ui.bigFont = CreateFontIndirectW(&big);

    ui.smallFont = CreateFontIndirectW(&base);
}

// Page Musique ; renvoie le bas de la page (DIP).
int layoutPitch(int top, bool shown) {
    const int margin = 20;
    const int inner = kWidth - 2 * margin;

    place(g.label, margin, top + 6, inner, 44, ui.bigFont);
    place(g.slider, margin, top + 54, inner, 34, ui.font);

    // Les graduations −4 … +4, alignées sur les crans du curseur.
    int pos[kTickCount];
    for (int i = 1; i < kTickCount - 1; ++i) pos[i] = int(SendMessageW(g.slider, TBM_GETTICPOS, WPARAM(i - 1), 0));
    int step = (pos[kTickCount - 2] - pos[1]) / (kTickCount - 3);
    pos[0] = pos[1] - step;
    pos[kTickCount - 1] = pos[kTickCount - 2] + step;
    for (int i = 0; i < kTickCount; ++i) {
        int center = S(margin) + pos[i];
        MoveWindow(g.ticks[i], center - S(16), S(top + 88), S(32), S(18), TRUE);
        SendMessageW(g.ticks[i], WM_SETFONT, WPARAM(ui.smallFont), TRUE);
    }

    place(g.reset, margin, top + 118, 170, 32, ui.font);
    place(g.toggle, kWidth - margin - 170, top + 118, 170, 32, ui.font);
    place(g.status, margin, top + 162, inner, 40, ui.smallFont);
    place(g.settings, margin, top + 208, 110, 26, ui.smallFont);
    place(g.sasLabel, margin, top + 246, inner, 20, ui.smallFont);
    place(g.sasCombo, margin, top + 266, inner, 240, ui.smallFont);
    place(g.outLabel, margin, top + 300, inner, 20, ui.smallFont);
    place(g.outCombo, margin, top + 320, inner, 240, ui.smallFont);

    HWND main[] = {g.label, g.slider, g.reset, g.toggle, g.status, g.settings};
    for (HWND h : main) ShowWindow(h, shown ? SW_SHOW : SW_HIDE);
    for (HWND h : g.ticks) ShowWindow(h, shown ? SW_SHOW : SW_HIDE);
    for (HWND h : {g.sasLabel, g.sasCombo, g.outLabel, g.outCombo})
        ShowWindow(h, shown && g.settingsShown ? SW_SHOW : SW_HIDE);
    return top + (g.settingsShown ? 360 : 246);
}

// ---------------------------------------------------------------- page « Appareils »

// Remplit les deux listes : « Automatique » d'abord, puis les vrais appareils (pas les câbles virtuels).
void fillDevicesPage() {
    auto fill = [](HWND combo, std::vector<std::wstring>& ids, const std::vector<OutputDevice>& list, const wchar_t* automatic,
                   const std::wstring& chosen) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        ids.assign(1, L"");
        SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(automatic));
        int selected = 0;
        for (const auto& device : list) {
            if (isVirtualDevice(device.name)) continue;
            if (!chosen.empty() && sameText(device.endpoint, chosen)) selected = int(ids.size());
            ids.push_back(device.endpoint);
            SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(device.name.c_str()));
        }
        SendMessageW(combo, CB_SETCURSEL, WPARAM(selected), 0);
    };
    fill(d.outCombo, d.outIds, g.engine.outputs(), L"Automatique (sortie par défaut de Windows)", iniRead(L"vocoder", L"sortie"));
    fill(d.micCombo, d.micIds, voicepage::microphones(), L"Automatique (micro par défaut de Windows)", iniRead(L"voix", L"micro"));
    setText(d.outNow, L"En ce moment : " + (ui.listenName.empty() ? std::wstring(L"(aucune sortie)") : ui.listenName) +
                          L". La musique transposée, le retour de ta voix et les sons du soundboard sortent ici.");
    setText(d.micNow, L"En ce moment : " + voicepage::currentMicName() +
                          L". C'est ce micro qui reçoit l'effet, puis part vers Discord, WhatsApp… par le micro virtuel.");
    // Les câbles virtuels : qui sert à quoi ; avec un seul câble, on choisit à quoi il sert.
    const cables::Assignment& c = g.cables;
    auto cableName = [](int i) { return i >= 0 ? g.devices[size_t(i)].name.substr(0, g.devices[size_t(i)].name.find(L" (")) : std::wstring(L"aucun"); };
    SendMessageW(d.cableCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(d.cableCombo, CB_ADDSTRING, 0, LPARAM(L"Il sert à la voix et au soundboard (Discord…)"));
    SendMessageW(d.cableCombo, CB_ADDSTRING, 0, LPARAM(L"Il sert à la musique (transposition)"));
    SendMessageW(d.cableCombo, CB_SETCURSEL, c.singleToMusic ? 1 : 0, 0);
    if (c.count == 0)
        setText(d.cableNow, L"Aucun câble virtuel. Installe VB-CABLE (gratuit, vb-audio.com/Cable), puis redémarre : voir le guide.");
    else if (c.count == 1)
        setText(d.cableNow, L"Un seul câble virtuel (" + cableName(c.music >= 0 ? c.music : c.voice) +
                                L"). Pour la musique ET la voix, ajoute un 2e câble gratuit : Hi-Fi Cable (voir le guide).");
    else
        setText(d.cableNow, L"Musique : " + cableName(c.music) + L"  ·  Voix et soundboard : " + cableName(c.voice));
}

// Un nouveau choix s'applique tout de suite : la musique et la voix repartent sur le bon appareil.
void onDeviceChosen(int id) {
    HWND combo = id == IDC_DEV_OUT ? d.outCombo : d.micCombo;
    const auto& ids = id == IDC_DEV_OUT ? d.outIds : d.micIds;
    int sel = int(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= int(ids.size())) return;
    if (id == IDC_DEV_OUT) {
        // Une seule sortie pour tout : la musique transposée, le retour de la voix et le soundboard.
        iniWrite(L"vocoder", L"sortie", ids[size_t(sel)]);
        iniWrite(L"voix", L"retour", ids[size_t(sel)]);
        bool wasRunning = g.engine.running() || !g.error.empty();
        stopPitch();  // rend d'abord la sortie par défaut d'origine (le mode automatique la lit)
        refreshDevices();
        if (wasRunning) startPitch();
        voicepage::reloadDevices();
        soundpage::reloadDevices();
    } else {
        iniWrite(L"voix", L"micro", ids[size_t(sel)]);
        voicepage::reloadDevices();
    }
    updateUi();
    fillDevicesPage();
}

// Un seul câble virtuel : on le donne à la voix ou à la musique. Celui qui l'avait le rend d'abord.
void onCableChosen() {
    int sel = int(SendMessageW(d.cableCombo, CB_GETCURSEL, 0, 0));
    iniWrite(L"appareils", L"cable_unique", sel == 1 ? L"musique" : L"voix");
    bool musicActive = g.engine.running() || !g.error.empty();
    stopPitch();
    voicepage::reloadDevices();
    refreshDevices();
    if (musicActive || sel == 1) startPitch();
    updateUi();
    fillDevicesPage();
    PostMessageW(ui.wnd, voicepage::WM_APP_RELAYOUT, 0, 0);  // la liste du câble unique apparaît ou disparaît
}

int layoutDevices(int top, bool shown) {
    const int margin = 20, inner = kWidth - 2 * margin;
    int y = top + 6;
    place(d.title, margin, y, inner, 28, ui.font);
    y += 32;
    place(d.intro, margin, y, inner, 40, ui.smallFont);
    y += 46;
    place(d.outLabel, margin, y, inner, 20, ui.smallFont);
    y += 22;
    place(d.outCombo, margin, y, inner, 300, ui.font);
    y += 34;
    place(d.outNow, margin, y, inner, 36, ui.smallFont);
    y += 50;
    place(d.micLabel, margin, y, inner, 20, ui.smallFont);
    y += 22;
    place(d.micCombo, margin, y, inner, 300, ui.font);
    y += 34;
    place(d.micNow, margin, y, inner, 36, ui.smallFont);
    y += 50;
    place(d.cableLabel, margin, y, inner, 20, ui.smallFont);
    y += 22;
    const bool single = g.cables.count == 1;
    if (single) {
        place(d.cableCombo, margin, y, inner, 300, ui.font);
        y += 34;
    }
    place(d.cableNow, margin, y, inner, 36, ui.smallFont);
    y += 50;
    ShowWindow(d.cableCombo, shown && single ? SW_SHOW : SW_HIDE);
    place(d.back, margin, y, 150, 32, ui.font);
    y += 44;
    for (HWND h : {d.title, d.intro, d.outLabel, d.outCombo, d.outNow, d.micLabel, d.micCombo, d.micNow, d.cableLabel, d.cableNow, d.back})
        ShowWindow(h, shown ? SW_SHOW : SW_HIDE);
    return y;
}

void createDevicesPage() {
    d.title = makeControl(L"STATIC", L"Appareils", SS_LEFT | SS_CENTERIMAGE);
    d.intro = makeControl(L"STATIC", L"« Automatique » suit les appareils par défaut de Windows (ceux d'avant SoundZ). "
                                     L"Tu peux aussi en choisir un précis.", SS_LEFT);
    d.outLabel = makeControl(L"STATIC", L"Sortie (là où tu écoutes)", SS_LEFT);
    d.outCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_DEV_OUT);
    d.outNow = makeControl(L"STATIC", L"", SS_LEFT);
    d.micLabel = makeControl(L"STATIC", L"Micro", SS_LEFT);
    d.micCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_DEV_MIC);
    d.micNow = makeControl(L"STATIC", L"", SS_LEFT);
    d.cableLabel = makeControl(L"STATIC", L"Câbles virtuels", SS_LEFT);
    d.cableCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_DEV_CABLE);
    d.cableNow = makeControl(L"STATIC", L"", SS_LEFT);
    theme::darkControl(d.cableCombo);
    d.back = theme::button(L"← Retour", theme::Kind::kButton, IDC_DEV_BACK);
    theme::darkControl(d.outCombo);
    theme::darkControl(d.micCombo);
}

// ---------------------------------------------------------------- 🪳 signaler un bug, proposer une fonctionnalité

constexpr wchar_t kIssuesUrl[] = L"https://github.com/Smooginozaure/SoundZ/issues/new";

// Version de SoundZ, lue dans les propriétés de l'exe (app.rc).
std::wstring soundzVersion() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    DWORD ignored = 0, size = GetFileVersionInfoSizeW(exe, &ignored);
    std::vector<BYTE> data(size);
    wchar_t* value = nullptr;
    UINT length = 0;
    if (size && GetFileVersionInfoW(exe, 0, size, data.data()) &&
        VerQueryValueW(data.data(), L"\\StringFileInfo\\040C04B0\\ProductVersion", reinterpret_cast<void**>(&value), &length) && value)
        return value;
    return L"?";
}

// « Windows 11 24H2 (10.0.26100) » : le vrai numéro, même si l'exe n'est pas déclaré pour la dernière version.
std::wstring windowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW info = {sizeof(info)};
    if (auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) get(&info);
    wchar_t display[64] = {};
    DWORD size = sizeof(display);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", RRF_RT_REG_SZ, nullptr,
                 display, &size);
    std::wstring name = info.dwBuildNumber >= 22000 ? L"Windows 11" : L"Windows 10";
    return name + (display[0] ? L" " + std::wstring(display) : L"") + L" (" + std::to_wstring(info.dwMajorVersion) + L"." +
           std::to_wstring(info.dwMinorVersion) + L"." + std::to_wstring(info.dwBuildNumber) + L")";
}

// Ce qui aide à comprendre un problème : versions, câbles virtuels, appareils utilisés. Aucune donnée personnelle ;
// la personne voit tout dans le navigateur avant d'envoyer.
std::wstring environmentReport() {
    auto cableName = [](int i) {
        return i >= 0 ? g.devices[size_t(i)].name.substr(0, g.devices[size_t(i)].name.find(L" (")) : std::wstring(L"aucun");
    };
    const wchar_t* tabs[] = {L"Musique", L"Voix (micro)", L"Soundboard", L"Appareils"};
    std::wstring out = iniRead(L"vocoder", L"sortie");
    std::wstring mic = iniRead(L"voix", L"micro");
    return L"SoundZ : " + soundzVersion() + L"\n" + L"Windows : " + windowsVersion() + L"\n" + L"Câbles virtuels : " +
           std::to_wstring(g.cables.count) + L" trouvé(s) ; musique → " + cableName(g.cables.music) + L", voix → " +
           cableName(g.cables.voice) + L"\n" + L"Sortie : " + (ui.listenName.empty() ? std::wstring(L"aucune") : ui.listenName) +
           (out.empty() ? L" (automatique)" : L" (choisie)") + L"\n" + L"Micro : " + voicepage::currentMicName() +
           (mic.empty() ? L" (automatique)" : L" (choisi)") + L"\n" + L"Musique : " +
           (g.engine.running() ? L"active" : g.error.empty() ? L"désactivée" : L"erreur — " + g.error) + L"\n" +
           L"Voix : " + (voicepage::running() ? L"active" : L"inactive") + L"\n" + L"Onglet ouvert : " +
           tabs[std::clamp(g_page, 0, 3)];
}

std::wstring urlEncode(const std::wstring& text) {
    std::string utf8 = narrow(text);
    std::wstring out;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += wchar_t(c);
        } else {
            out += L'%';
            out += wchar_t(hex[c >> 4]);
            out += wchar_t(hex[c & 15]);
        }
    }
    return out;
}

// Ouvre le formulaire GitHub (bug.yml ou feature.yml), avec la case « Ton installation » déjà remplie.
void openIssue(const wchar_t* form) {
    std::wstring url = std::wstring(kIssuesUrl) + L"?template=" + form + L"&environnement=" + urlEncode(environmentReport());
    ShellExecuteW(ui.wnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void showFeedback() {
    enum { kBug = 1, kFeature };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kBug, L"Signaler un bug…");
    AppendMenuW(menu, MF_STRING, kFeature, L"Proposer une fonctionnalité…");
    RECT r;
    GetWindowRect(g_feedback, &r);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom + S(4), 0, ui.wnd, nullptr);
    DestroyMenu(menu);
    if (choice == kBug) openIssue(L"bug.yml");
    if (choice == kFeature) openIssue(L"feature.yml");
}

// Le bouton 🪳 : l'emoji en couleur, surligné au survol (comme les onglets).
void drawFeedbackButton(const DRAWITEMSTRUCT* d) {
    RECT rc = d->rcItem;
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC dc = CreateCompatibleDC(d->hDC);
    HBITMAP canvas = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ old = SelectObject(dc, canvas);
    RECT all = {0, 0, w, h};
    FillRect(dc, &all, theme::brush(theme::kBg));
    const bool pressed = d->itemState & ODS_SELECTED, hover = theme::isHover(d->hwndItem);
    if (hover || pressed) theme::roundRect(dc, all, float(S(6)), pressed ? theme::kSurfacePressed : theme::kSurface);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) theme::roundFrame(dc, all, float(S(6)), theme::kAccentLight, 1.5f);
    const int px = S(18);
    if (g_bugImage.empty()) soundimg::renderText(L"🪳", g_bugImage);
    if (!g_bugBitmap || g_bugPx != px) {
        if (g_bugBitmap) DeleteObject(g_bugBitmap);
        g_bugBitmap = soundimg::makeBitmap(g_bugImage, px);
        g_bugPx = px;
    }
    soundimg::draw(dc, g_bugBitmap, (w - px) / 2, (h - px) / 2 + (pressed ? 1 : 0), px);
    BitBlt(d->hDC, rc.left, rc.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(canvas);
    DeleteDC(dc);
}

// Bulles d'aide au survol des deux boutons ronds (thème sombre de Windows).
void createTooltips() {
    g_tooltips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, ui.wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(g_tooltips, L"DarkMode_Explorer", nullptr);
    const struct { HWND control; const wchar_t* text; } tips[] = {
        {g_feedback, L"Signaler un bug  ·  Proposer une fonctionnalité"},
        {g_options, L"Options : appareils, raccourcis, lancement avec Windows"},
    };
    for (const auto& t : tips) {
        TOOLINFOW info = {};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = ui.wnd;
        info.uId = UINT_PTR(t.control);
        info.lpszText = const_cast<wchar_t*>(t.text);
        SendMessageW(g_tooltips, TTM_ADDTOOLW, 0, LPARAM(&info));
    }
    SendMessageW(g_tooltips, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
}

// Windows calcule parfois mal, au lancement, la largeur du bouton de SoundZ dans la barre des tâches (44 px :
// l'icône seule, le nom coupé). On lui fait refaire ce bouton une fois la fenêtre affichée.
void refreshTaskbarButton() {
    ITaskbarList* taskbar = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar)))) return;
    if (SUCCEEDED(taskbar->HrInit())) {
        taskbar->DeleteTab(ui.wnd);
        taskbar->AddTab(ui.wnd);
    }
    taskbar->Release();
}

void layout() {
    createFonts();
    // Les onglets : des « pastilles », l'onglet ouvert en teinte Orage.
    const int tabWidth[3] = {80, 106, 102};
    for (int i = 0, x = 12; i < 3; x += tabWidth[i] + 6, ++i) place(g_tabs[i], x, 8, tabWidth[i], 30, ui.font);
    place(g_options, kWidth - 12 - 34, 8, 34, 30, ui.font);
    place(g_feedback, kWidth - 12 - 34 - 4 - 34, 8, 34, 30, ui.font);
    int pitchBottom = layoutPitch(kPageTop, g_page == 0);
    int voiceBottom = voicepage::layout(kPageTop, g_page == 1);
    int soundBottom = soundpage::layout(kPageTop, g_page == 2);
    int devicesBottom = layoutDevices(kPageTop, g_page == kDevicesPage);
    int bottom = g_page == 0 ? pitchBottom : g_page == 1 ? voiceBottom : g_page == 2 ? soundBottom : devicesBottom;

    RECT rect = {0, 0, S(kWidth), S(bottom + 4)};
    DWORD style = DWORD(GetWindowLongPtrW(ui.wnd, GWL_STYLE));
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, ui.dpi);
    SetWindowPos(ui.wnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(ui.wnd, nullptr, TRUE);
}

// Icône dessinée au lancement : carré arrondi teinte Orage avec « SZ » en blanc (comme assets/icon.svg).
HICON makeIcon(int size) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    HGDIOBJ oldBitmap = SelectObject(dc, color);

    HBRUSH red = CreateSolidBrush(theme::kAccent);
    HGDIOBJ oldBrush = SelectObject(dc, red);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    int radius = size * 14 / 32;
    RoundRect(dc, 0, 0, size + 1, size + 1, radius, radius);

    HFONT font = CreateFontW(-size * 11 / 20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r = {0, 0, size, size};
    DrawTextW(dc, L"SZ", 2, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();

    // GDI n'écrit pas la transparence : tout pixel dessiné devient opaque.
    auto* px = static_cast<uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) {
        if (px[i] & 0x00FFFFFF) px[i] |= 0xFF000000;
    }

    SelectObject(dc, oldFont);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldBitmap);
    DeleteObject(font);
    DeleteObject(red);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO icon = {TRUE, 0, 0, mask, color};
    HICON result = CreateIconIndirect(&icon);
    DeleteObject(mask);
    DeleteObject(color);
    return result;
}

// ---------------------------------------------------------------- fenêtre

LRESULT CALLBACK sliderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    // Molette vers le haut / flèche haut = plus aigu (le curseur Windows fait l'inverse par défaut).
    if (msg == WM_MOUSEWHEEL) {
        applySemitones(sliderPos(g.slider) + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1));
        return 0;
    }
    if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN)) {
        applySemitones(sliderPos(g.slider) + (wp == VK_UP ? 1 : -1));
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void createControls() {
    const wchar_t* tabNames[3] = {L"Musique", L"Voix (micro)", L"Soundboard"};
    for (int i = 0; i < 3; ++i) {
        g_tabs[i] = theme::button(tabNames[i], theme::Kind::kTab, IDC_TAB0 + i);
        ShowWindow(g_tabs[i], SW_SHOW);
    }
    g_options = theme::button(L"⚙", theme::Kind::kTab, IDC_OPTIONS);
    ShowWindow(g_options, SW_SHOW);
    g_feedback = theme::button(L"", theme::Kind::kTile, IDC_FEEDBACK);  // dessiné ici : l'emoji 🪳 en couleur
    ShowWindow(g_feedback, SW_SHOW);
    createTooltips();

    g.label = makeControl(L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE);
    g.slider = makeSlider(PitchEngine::kMinSemitones, PitchEngine::kMaxSemitones, TBS_AUTOTICKS);
    theme::slider(g.slider, true);  // rempli depuis le 0 central
    SetWindowSubclass(g.slider, sliderProc, 1, 0);
    for (int i = 0; i < kTickCount; ++i) {
        int value = PitchEngine::kMinSemitones + i;
        std::wstring text = value > 0 ? L"+" + std::to_wstring(value)
                            : value < 0 ? L"−" + std::to_wstring(-value)
                                        : L"0";
        g.ticks[i] = makeControl(L"STATIC", text.c_str(), SS_CENTER);
    }
    g.reset = theme::button(L"Remettre à 0", theme::Kind::kButton, IDC_RESET);
    g.toggle = theme::button(L"Désactiver", theme::Kind::kButton, IDC_TOGGLE);
    g.status = makeControl(L"STATIC", L"", SS_LEFT);
    g.settings = theme::button(L"Réglages", theme::Kind::kButton, IDC_SETTINGS);
    g.sasLabel = makeControl(L"STATIC", L"Sas de la musique (câble virtuel que personne n'écoute) :", SS_LEFT);
    g.sasCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_SAS);
    g.outLabel = makeControl(L"STATIC", L"Sortie (là où tu écoutes) :", SS_LEFT);
    g.outCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_OUT);
    theme::darkControl(g.sasCombo);
    theme::darkControl(g.outCombo);

    createDevicesPage();
    voicepage::create();
    soundpage::create();
}

void onComboChanged(int id) {
    HWND combo = id == IDC_SAS ? g.sasCombo : g.outCombo;
    int index = int(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= int(g.devices.size())) return;
    (id == IDC_SAS ? g.sasIndex : g.outIndex) = index;
    iniWrite(L"vocoder", id == IDC_SAS ? L"sas" : L"sortie", g.devices[size_t(index)].endpoint);
    if (g.engine.running() || !g.error.empty()) startPitch();
    if (id == IDC_SAS) voicepage::reloadDevices();  // la voix ne doit pas garder le câble que la musique vient de prendre
    updateUi();
}

// « SoundZ.exe --devicetest rapport.txt » : quels appareils SoundZ choisirait sur ce PC (rien n'est ouvert ni changé).
int runDeviceTest(const std::wstring& reportPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    ui.iniPath = exe;
    ui.iniPath = ui.iniPath.substr(0, ui.iniPath.find_last_of(L"\\/") + 1) + L"SoundZ.ini";
    std::wstring report, error;
    if (!g.engine.init(error)) {
        report = L"son de Windows inaccessible\r\n";
    } else {
        refreshDevices();
        std::wstring def = getDefaultOutput(), defName = L"(aucune)";
        for (const auto& d : g.devices)
            if (sameText(d.endpoint, def)) defName = d.name;
        auto name = [](int i) { return i >= 0 ? g.devices[size_t(i)].name : std::wstring(L"(aucune)"); };
        report = L"sortie par défaut de Windows : " + defName + L"\r\nsortie d'écoute choisie : " + name(g.outIndex) +
                 L"\r\nsas de la musique : " + name(g.sasIndex) + L"\r\n";
    }
    report += voicepage::deviceReport();
    report += L"\r\n--- case « Ton installation » d'un ticket 🪳 ---\r\n" + environmentReport() + L"\r\n\r\n--- lien du ticket ---\r\n" +
              std::wstring(kIssuesUrl) + L"?template=bug.yml&environnement=" + urlEncode(environmentReport()) + L"\r\n";
    std::string utf8 = narrow(report);
    if (FILE* f = _wfopen(reportPath.c_str(), L"wb")) {
        fwrite("\xEF\xBB\xBF", 1, 3, f);
        fwrite(utf8.data(), 1, utf8.size(), f);
        fclose(f);
    }
    CoUninitialize();
    return 0;
}

void showPage(int page) {
    if (page == kDevicesPage) {
        if (g_page != kDevicesPage) g_lastTab = g_page;
        fillDevicesPage();
    } else {
        iniWrite(L"fenetre", L"onglet", std::to_wstring(page));  // la page « Appareils » n'est pas retenue
    }
    g_page = page;
    for (int i = 0; i < 3; ++i) SendMessageW(g_tabs[i], BM_SETCHECK, i == page ? BST_CHECKED : BST_UNCHECKED, 0);
    layout();
}

void shutdownAll() {
    stopPitch();
    voicepage::shutdown();
    soundpage::shutdown();
}

LRESULT CALLBACK windowProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == g.slider) applySemitones(sliderPos(g.slider));
        else if (!voicepage::onScroll(reinterpret_cast<HWND>(lp))) soundpage::onScroll(reinterpret_cast<HWND>(lp), LOWORD(wp));
        return 0;

    case WM_NOTIFY: {
        LRESULT result = 0;
        if (theme::customDraw(reinterpret_cast<NMHDR*>(lp), result)) return result;  // curseurs
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        theme::paint(dc);  // cadres arrondis des champs de saisie
        EndPaint(wnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        theme::onCommand(reinterpret_cast<HWND>(lp), HIWORD(wp));  // interrupteurs, cadres des champs
        if (LOWORD(wp) >= IDC_TAB0 && LOWORD(wp) < IDC_TAB0 + 3 && HIWORD(wp) == BN_CLICKED) {
            showPage(LOWORD(wp) - IDC_TAB0);
            return 0;
        }
        if (LOWORD(wp) == IDC_FEEDBACK && HIWORD(wp) == BN_CLICKED) {
            showFeedback();
            return 0;
        }
        if (LOWORD(wp) == IDC_OPTIONS && HIWORD(wp) == BN_CLICKED) {
            showOptions();
            return 0;
        }
        switch (LOWORD(wp)) {
        case IDC_RESET:
            applySemitones(0);
            SetFocus(g.slider);
            return 0;
        case IDC_TOGGLE:
            if (g.engine.running()) {
                stopPitch();
                g.error.clear();
            } else {
                refreshDevices();
                startPitch();
            }
            updateUi();
            return 0;
        case IDC_SETTINGS:
            g.settingsShown = !g.settingsShown;
            layout();
            updateUi();
            return 0;
        case IDC_DEV_OUT:
        case IDC_DEV_MIC:
            if (HIWORD(wp) == CBN_SELCHANGE) onDeviceChosen(LOWORD(wp));
            return 0;
        case IDC_DEV_CABLE:
            if (HIWORD(wp) == CBN_SELCHANGE) onCableChosen();
            return 0;
        case IDC_DEV_BACK:
            showPage(g_lastTab);
            return 0;
        case IDC_DEV_OPEN:
            showPage(kDevicesPage);
            return 0;
        case IDC_QUIT:
            g_quitting = true;
            PostMessageW(wnd, WM_CLOSE, 0, 0);
            return 0;
        case IDC_SAS:
        case IDC_OUT:
            if (HIWORD(wp) == CBN_SELCHANGE) onComboChanged(LOWORD(wp));
            return 0;
        }
        if (voicepage::onCommand(wp, lp)) return 0;
        if (soundpage::onCommand(wp, lp)) return 0;
        break;

    case WM_DRAWITEM:
        if (theme::drawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lp))) return TRUE;
        if (reinterpret_cast<const DRAWITEMSTRUCT*>(lp)->CtlID == IDC_FEEDBACK) {
            drawFeedbackButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        }
        if (soundpage::onDrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lp))) return TRUE;
        break;

    case WM_CONTEXTMENU:
        if (soundpage::onContextMenu(reinterpret_cast<HWND>(wp), GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
        break;

    case WM_HOTKEY:
        soundpage::onHotkey(int(wp));
        return 0;

    case WM_TIMER:
        if (wp == kStatusTimer) updateStatus();
        if (wp == kVolumeTimer) followVolume();
        if (wp == kTaskbarTimer || wp == kTaskbarTimer2) {
            KillTimer(wnd, wp);
            refreshTaskbarButton();
        }
        if (wp == kVoiceTimer) {
            voicepage::onTimer();
            soundpage::onTimer();
        }
        return 0;

    case WM_APP_DEVICE_LOST:
        stopPitch();
        refreshDevices();
        g.error = L"Une sortie audio a disparu (casque débranché ?). Transposition arrêtée — clique sur Activer pour relancer.";
        updateUi();
        return 0;

    case voicepage::WM_APP_VOICE_LOST:
        voicepage::onDeviceLost();
        return 0;

    case voicepage::WM_APP_RELAYOUT:
        layout();
        return 0;

    case voicepage::WM_APP_CABLES_CHANGED:  // la voix a changé de câble : la musique prend un câble encore libre
        if (g.engine.running() || !g.error.empty()) {
            stopPitch();
            refreshDevices();
            startPitch();
        } else {
            refreshDevices();
        }
        updateUi();
        return 0;

    case WM_CTLCOLORSTATIC: {
        HWND control = reinterpret_cast<HWND>(lp);
        COLORREF text = theme::kText;
        bool voiceError = false;
        if (control == g.status) text = g.error.empty() ? theme::kMuted : theme::kDanger;
        else if (voicepage::isStatus(control, voiceError) || soundpage::isStatus(control, voiceError))
            text = voiceError ? theme::kDanger : theme::kMuted;
        else if (control == g.sasLabel || control == g.outLabel) text = theme::kText2;
        else if (control == d.intro || control == d.outNow || control == d.micNow || control == d.cableNow) text = theme::kMuted;
        else if (control == d.outLabel || control == d.micLabel || control == d.cableLabel) text = theme::kText2;
        else
            for (HWND tick : g.ticks)
                if (control == tick) text = theme::kFaint;
        return theme::ctlColor(msg, reinterpret_cast<HDC>(wp), control, text);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return theme::ctlColor(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp), theme::kText);

    case WM_DPICHANGED: {
        ui.dpi = HIWORD(wp);
        auto* suggested = reinterpret_cast<RECT*>(lp);
        SetWindowPos(wnd, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wp) shutdownAll();  // extinction / déconnexion : on rend la sortie et le micro à Windows
        return 0;

    case WM_CLOSE:
        // La croix (ou Alt+F4) réduit seulement, si l'option est cochée : la musique, la voix et le soundboard
        // continuent. « Quitter SoundZ » (menu ⚙) ferme pour de bon.
        if (minimizeOnClose() && !g_quitting) {
            ShowWindow(wnd, SW_MINIMIZE);
            return 0;
        }
        shutdownAll();
        DestroyWindow(wnd);
        return 0;

    case WM_DESTROY:
        KillTimer(wnd, kStatusTimer);
        KillTimer(wnd, kVolumeTimer);
        KillTimer(wnd, kVoiceTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void onDeviceLost(void*) { PostMessageW(ui.wnd, WM_APP_DEVICE_LOST, 0, 0); }

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && std::wstring(argv[1]) == L"--selftest") return runSelfTest(argv[2]);
    if (argc >= 4 && std::wstring(argv[1]) == L"--enginetest") return runEngineTest(argv[2], argv[3]);
    if (argc >= 3 && std::wstring(argv[1]) == L"--voicetest") return runVoiceTest(argv[2]);
    if (argc >= 3 && std::wstring(argv[1]) == L"--soundtest") return runSoundTest(argv[2]);
    if (argc >= 4 && std::wstring(argv[1]) == L"--claudetest") return runClaudeTest(argv[2], argv[3]);
    if (argc >= 3 && std::wstring(argv[1]) == L"--shelltest") return runShellTest(argv[2]);
    if (argc >= 3 && std::wstring(argv[1]) == L"--devicetest") return runDeviceTest(argv[2]);
    if (argc >= 3 && std::wstring(argv[1]) == L"--cabletest") return runCableTest(argv[2]);

    // Une seule instance : si le programme tourne déjà, on ramène sa fenêtre.
    CreateMutexW(nullptr, TRUE, L"Local\\SoundZ.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClassName, nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }
    // L'ancienne version (« Vocoder ») ouverte en même temps se battrait pour les mêmes sorties audio.
    if (FindWindowW(L"VocoderPitchWindow", nullptr)) {
        MessageBoxW(nullptr, L"L'ancienne version (Vocoder) est encore ouverte. Ferme-la, puis relance SoundZ.", L"SoundZ",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls = {sizeof(controls),
                                     ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_HOTKEY_CLASS};
    InitCommonControlsEx(&controls);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    ui.iniPath = exePath;
    const std::wstring folder = ui.iniPath.substr(0, ui.iniPath.find_last_of(L"\\/") + 1);
    ui.iniPath = folder + L"SoundZ.ini";
    // Réglages de l'ancienne version (Vocoder.ini) : repris tels quels, y compris la réparation après plantage.
    if (GetFileAttributesW(ui.iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        MoveFileW((folder + L"Vocoder.ini").c_str(), ui.iniPath.c_str());

    theme::init();
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = theme::brush(theme::kBg);
    wc.lpszClassName = kClassName;
    // Icône « SZ » intégrée à l'exe (assets/icon.ico) ; dessinée à la volée si elle manque.
    wc.hIcon = HICON(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wc.hIconSm = HICON(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!wc.hIcon) wc.hIcon = makeIcon(GetSystemMetrics(SM_CXICON));
    if (!wc.hIconSm) wc.hIconSm = makeIcon(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    ui.wnd = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, nullptr, nullptr, instance, nullptr);
    ui.dpi = GetDpiForWindow(ui.wnd);
    theme::decorateWindow(ui.wnd);
    createControls();
    g_page = std::clamp(iniReadInt(L"fenetre", L"onglet", 0), 0, 2);
    for (int i = 0; i < 3; ++i) SendMessageW(g_tabs[i], BM_SETCHECK, i == g_page ? BST_CHECKED : BST_UNCHECKED, 0);
    layout();

    std::wstring error;
    if (!g.engine.init(error)) {
        g.error = error;
    } else {
        g.engine.setLostCallback(onDeviceLost, nullptr);
        repairAfterCrash();
        refreshDevices();
        startPitch();
    }
    voicepage::startup();
    soundpage::startup();
    shellint::refresh();  // SoundZ a changé de dossier ? ses raccourcis suivent
    applySemitones(0);
    updateUi();
    SetTimer(ui.wnd, kStatusTimer, 500, nullptr);
    SetTimer(ui.wnd, kVolumeTimer, 100, nullptr);
    SetTimer(ui.wnd, kVoiceTimer, 50, nullptr);

    // Lancé avec Windows : SoundZ démarre réduit dans la barre des tâches (tout tourne déjà).
    bool atStartup = false;
    for (int i = 1; i < argc; ++i) atStartup = atStartup || std::wstring(argv[i]) == shellint::kStartupArgument;
    ShowWindow(ui.wnd, atStartup ? SW_SHOWMINNOACTIVE : show);
    SetTimer(ui.wnd, kTaskbarTimer, 1500, nullptr);
    SetTimer(ui.wnd, kTaskbarTimer2, 5000, nullptr);
    UpdateWindow(ui.wnd);
    SetFocus(g_page == 0 ? g.slider : g_tabs[g_page]);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(ui.wnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    theme::shutdown();
    CoUninitialize();
    return 0;
}
