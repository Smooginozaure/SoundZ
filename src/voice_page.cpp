#include "voice_page.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cables.h"
#include "default_output.h"
#include "sound_page.h"
#include "ui.h"
#include "util.h"
#include "voice_engine.h"

namespace voicepage {
namespace {

enum ControlId : int {
    IDC_MODE_NONE = 300,
    IDC_MODE_TUNE,
    IDC_MODE_VOCODER,
    IDC_CHORDS,
    IDC_MONITOR,
    IDC_PRESET_NATURAL,
    IDC_PRESET_TPAIN,
    IDC_PRESET_ROBOT,
    IDC_PRESET_CHOIR,
    IDC_VTOGGLE,
    IDC_VSETTINGS,
    IDC_MIC,
    IDC_CABLE,
    IDC_HEADSET,
};

const wchar_t* const kNoteNames[12] = {L"Do", L"Do♯", L"Ré", L"Mi♭", L"Mi", L"Fa",
                                       L"Fa♯", L"Sol", L"La♭", L"La", L"Si♭", L"Si"};
const wchar_t* const kScaleNames[voice::kScaleCount] = {L"majeure", L"mineure", L"penta. majeure",
                                                        L"penta. mineure", L"blues", L"chromatique"};
constexpr wchar_t kSection[] = L"voix";
constexpr wchar_t kRestore[] = L"restaurer_voix";

enum Row { kRowKey, kRowScale, kRowRetune, kRowAmount, kRowTranspose, kRowGate, kRowCount };
struct RowDef {
    const wchar_t* key;  // nom dans le .ini
    int minValue, maxValue, defaultValue;
};
const RowDef kRows[kRowCount] = {
    {L"tonalite", 0, 11, 0},     {L"gamme", 0, voice::kScaleCount - 1, 0}, {L"correction", 0, 100, 0},
    {L"intensite", 0, 100, 100}, {L"hauteur", -12, 12, 0},                 {L"porte", -80, -20, -50},
};

struct Page {
    HWND modes[3] = {};
    HWND note = nullptr, caption = nullptr;
    HWND rowLabel[kRowCount] = {}, rowSlider[kRowCount] = {};
    HWND levelLabel = nullptr, level = nullptr;
    HWND chords = nullptr, monitor = nullptr;
    HWND presetLabel = nullptr, presets[4] = {};
    HWND toggle = nullptr, settings = nullptr, status = nullptr;
    HWND micLabel = nullptr, micCombo = nullptr, cableLabel = nullptr, cableCombo = nullptr;
    HWND headsetLabel = nullptr, headsetCombo = nullptr;
    bool shown = false, settingsShown = false;

    VoiceEngine engine;
    bool engineReady = false;
    std::vector<OutputDevice> mics, outs;
    int mic = -1, cable = -1, headset = -1;
    int mode = voice::kModeAutotune;
    std::wstring error;

    // Micro par défaut de Windows, remplacé par CABLE-A Output pendant le fonctionnement.
    bool switched = false;
    std::wstring cableCapture;
    cables::Assignment cables;  // qui a quel câble virtuel (musique / voix)
    std::wstring lastMic;  // micro trouvé la dernière fois (si le micro par défaut devient le câble)
    std::wstring previous[3];
};

Page p;

// ---------------------------------------------------------------- textes

std::wstring noteName(int midi) {
    int pc = ((midi % 12) + 12) % 12;
    int octave = midi / 12 - 2;  // notation française : La3 = 440 Hz
    if (midi < 0 && midi % 12 != 0) octave -= 1;
    return std::wstring(kNoteNames[pc]) + std::to_wstring(octave);
}

std::wstring withCents(float midi) {
    int n = int(std::lround(midi));
    int cents = int(std::lround((midi - float(n)) * 100));
    std::wstring s = noteName(n);
    if (std::abs(cents) >= 5) s += (cents > 0 ? L" +" : L" −") + std::to_wstring(std::abs(cents)) + L" c";
    return s;
}

std::wstring rowText(int row, int v) {
    switch (row) {
    case kRowKey: return std::wstring(L"Tonalité : ") + kNoteNames[std::clamp(v, 0, 11)];
    case kRowScale: return std::wstring(L"Gamme : ") + kScaleNames[std::clamp(v, 0, voice::kScaleCount - 1)];
    case kRowRetune: return v == 0 ? L"Correction : instantanée" : L"Correction : " + std::to_wstring(v * 4) + L" ms";
    case kRowAmount: return L"Intensité : " + std::to_wstring(v) + L" %";
    case kRowTranspose:
        return v == 0 ? L"Hauteur : 0" : std::wstring(L"Hauteur : ") + (v > 0 ? L"+" : L"−") + std::to_wstring(std::abs(v));
    case kRowGate: return v <= -80 ? L"Porte de bruit : coupée" : L"Porte de bruit : −" + std::to_wstring(-v) + L" dB";
    }
    return L"";
}

// ---------------------------------------------------------------- réglages -> moteur

void applyRow(int row, bool save) {
    int v = sliderPos(p.rowSlider[row]);
    setText(p.rowLabel[row], rowText(row, v));
    switch (row) {
    case kRowKey: p.engine.setKey(v); break;
    case kRowScale: p.engine.setScale(v); break;
    case kRowRetune: p.engine.setRetuneMs(float(v * 4)); break;
    case kRowAmount: p.engine.setAmount(float(v) / 100.0f); break;
    case kRowTranspose: p.engine.setTranspose(v); break;
    case kRowGate: p.engine.setGateDb(float(v)); break;
    }
    if (save) iniWrite(kSection, kRows[row].key, std::to_wstring(v));
}

void applyMode(int mode, bool save) {
    p.mode = std::clamp(mode, 0, 2);
    for (int i = 0; i < 3; ++i) SendMessageW(p.modes[i], BM_SETCHECK, i == p.mode ? BST_CHECKED : BST_UNCHECKED, 0);
    p.engine.setMode(p.mode);
    EnableWindow(p.chords, p.mode == voice::kModeVocoder);
    if (save) iniWrite(kSection, L"mode", std::to_wstring(p.mode));
}

void applyChecks(bool save) {
    bool chords = SendMessageW(p.chords, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool monitor = SendMessageW(p.monitor, BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.engine.setChords(chords);
    p.engine.setMonitor(monitor);
    if (save) {
        iniWrite(kSection, L"accords", chords ? L"1" : L"0");
        iniWrite(kSection, L"casque", monitor ? L"1" : L"0");
    }
}

void setRow(int row, int v) {
    setSliderPos(p.rowSlider[row], v);
    applyRow(row, true);
}

void applyPreset(int id) {
    switch (id) {
    case IDC_PRESET_NATURAL:  // correction douce, la voix reste humaine
        applyMode(voice::kModeAutotune, true);
        setRow(kRowRetune, 30);
        setRow(kRowAmount, 70);
        break;
    case IDC_PRESET_TPAIN:  // correction instantanée : l'effet robotique des producteurs
        applyMode(voice::kModeAutotune, true);
        setRow(kRowRetune, 0);
        setRow(kRowAmount, 100);
        break;
    case IDC_PRESET_ROBOT:
        applyMode(voice::kModeVocoder, true);
        SendMessageW(p.chords, BM_SETCHECK, BST_UNCHECKED, 0);
        break;
    case IDC_PRESET_CHOIR:
        applyMode(voice::kModeVocoder, true);
        SendMessageW(p.chords, BM_SETCHECK, BST_CHECKED, 0);
        break;
    }
    applyChecks(true);
}

// ---------------------------------------------------------------- appareils

int findDevice(const std::vector<OutputDevice>& list, const std::wstring& preferred,
               std::initializer_list<const wchar_t*> names) {
    for (int i = 0; i < int(list.size()); ++i) {
        if (!preferred.empty() && sameText(list[size_t(i)].endpoint, preferred)) return i;
    }
    for (const wchar_t* name : names) {
        for (int i = 0; i < int(list.size()); ++i) {
            if (contains(list[size_t(i)].name, name)) return i;
        }
    }
    return -1;
}

void fillCombo(HWND combo, const std::vector<OutputDevice>& list, int selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& d : list) SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(d.name.c_str()));
    SendMessageW(combo, CB_SETCURSEL, WPARAM(selected), 0);
}

void refreshDevices() {
    p.mics = p.engine.microphones();
    p.outs = p.engine.outputs();
    // Le vrai micro, pour n'importe quel PC : celui choisi dans les réglages, sinon le micro par défaut de
    // Windows (s'il n'est pas virtuel), sinon le dernier utilisé, sinon le premier vrai micro.
    auto index = [](const std::vector<OutputDevice>& list, const std::wstring& endpoint) {
        for (int i = 0; i < int(list.size()); ++i)
            if (!endpoint.empty() && sameText(list[size_t(i)].endpoint, endpoint)) return i;
        return -1;
    };
    p.mic = index(p.mics, iniRead(kSection, L"micro"));
    if (p.mic < 0) {
        p.mic = index(p.mics, getDefaultDevice(true, 0));
        if (p.mic >= 0 && isVirtualDevice(p.mics[size_t(p.mic)].name)) p.mic = -1;
        if (p.mic >= 0) iniWrite(L"appareils", L"micro", p.mics[size_t(p.mic)].endpoint);  // retenu pour plus tard
    }
    if (p.mic < 0) p.mic = index(p.mics, p.lastMic);
    // Micro par défaut = un câble (par exemple juste après l'installation de VB-CABLE) : le dernier vrai micro
    // par défaut vu, sinon un vrai micro (pas une entrée ligne), sinon n'importe quel micro non virtuel.
    if (p.mic < 0) p.mic = index(p.mics, iniRead(L"appareils", L"micro"));
    for (int i = 0; i < int(p.mics.size()) && p.mic < 0; ++i)
        if (!isVirtualDevice(p.mics[size_t(i)].name) && isPersonalDevice(p.mics[size_t(i)].endpoint)) p.mic = i;
    for (int i = 0; i < int(p.mics.size()) && p.mic < 0; ++i)
        if (!isVirtualDevice(p.mics[size_t(i)].name)) p.mic = i;
    if (p.mic >= 0) p.lastMic = p.mics[size_t(p.mic)].endpoint;

    // Le micro virtuel des applis : un câble que la musique n'utilise pas (voir cables.h).
    {
        std::vector<std::wstring> names;
        for (const auto& o : p.outs) names.push_back(o.name);
        p.cables = cables::assign(names, index(p.outs, iniRead(L"vocoder", L"sas")), index(p.outs, iniRead(kSection, L"cable")),
                                  iniRead(L"appareils", L"cable_unique") == L"musique");
        p.cable = p.cables.voice;
    }
    // Retour casque : celui choisi, sinon là où l'on écoute (la sortie par défaut au lancement).
    p.headset = index(p.outs, iniRead(kSection, L"retour"));
    if (p.headset < 0) p.headset = index(p.outs, ui.listenOutput);
    if (p.headset < 0) p.headset = index(p.outs, iniRead(L"appareils", L"ecoute"));
    for (int i = 0; i < int(p.outs.size()) && p.headset < 0; ++i)
        if (!isVirtualDevice(p.outs[size_t(i)].name) && isPersonalDevice(p.outs[size_t(i)].endpoint)) p.headset = i;
    for (int i = 0; i < int(p.outs.size()) && p.headset < 0; ++i)
        if (!isVirtualDevice(p.outs[size_t(i)].name)) p.headset = i;
    fillCombo(p.micCombo, p.mics, p.mic);
    fillCombo(p.cableCombo, p.outs, p.cable);
    fillCombo(p.headsetCombo, p.outs, p.headset);
    const std::wstring where = p.headset >= 0 ? L"  ·  " + shortDeviceName(p.outs[size_t(p.headset)].name) : L"";
    setText(p.monitor, L"Entendre ma voix (écho)" + where);
}

// Micro virtuel vu par les applis : « CABLE-A Input » (sortie) devient « CABLE-A Output » (micro), de même pour
// CABLE, Hi-Fi Cable… Voicemeeter, lui, renvoie son entrée virtuelle vers « Voicemeeter Out B1 » (B2, B3).
std::wstring cableCaptureEndpoint() {
    if (p.cable < 0) return L"";
    std::wstring name = p.outs[size_t(p.cable)].name;
    if (cables::startsWith(name, L"Voicemeeter")) {
        const wchar_t* out = cables::startsWith(name, L"Voicemeeter AUX") ? L"Voicemeeter Out B2"
                             : cables::startsWith(name, L"Voicemeeter VAIO3") ? L"Voicemeeter Out B3"
                                                                               : L"Voicemeeter Out B1";
        for (const auto& m : p.mics)
            if (cables::startsWith(m.name, out)) return m.endpoint;
        return L"";
    }
    size_t at = name.find(L"Input");
    if (at == std::wstring::npos) return L"";
    name.replace(at, 5, L"Output");
    for (const auto& m : p.mics)
        if (sameText(m.name, name)) return m.endpoint;
    return L"";
}

// Nom du micro virtuel à choisir dans les applis (« CABLE-A Output »…).
std::wstring cableCaptureName() {
    for (const auto& m : p.mics)
        if (!p.cableCapture.empty() && sameText(m.endpoint, p.cableCapture)) return m.name.substr(0, m.name.find(L" ("));
    return L"le micro virtuel";
}

// ---------------------------------------------------------------- micro par défaut de Windows

void restoreDefaults(const std::wstring& cable, const std::wstring previous[3]) {
    for (int r = 0; r < 3; ++r) {
        if (!previous[r].empty() && sameText(getDefaultDevice(true, r), cable)) setDefaultDevice(previous[r], r);
    }
}

void stopVoice() {
    if (p.switched) {
        restoreDefaults(p.cableCapture, p.previous);
        iniDeleteSection(kRestore);
        p.switched = false;
    }
    p.engine.stop();
}

void startVoice() {
    stopVoice();
    p.error.clear();
    if (!p.engineReady) return;
    refreshDevices();
    if (p.mic < 0) { p.error = L"Aucun micro trouvé. Branche ton micro, puis clique sur Activer."; return; }
    if (p.cable < 0) {
        p.error = p.cables.count >= 1
                      ? L"Le seul câble virtuel sert à la musique. Donne-le à la voix (⚙ → Appareils), ou installe un "
                        L"2e câble (Hi-Fi Cable, gratuit)."
                      : L"Aucun câble virtuel : installe VB-CABLE (gratuit, vb-audio.com/Cable), redémarre, puis clique sur Activer.";
        return;
    }
    const OutputDevice* headset = p.headset >= 0 ? &p.outs[size_t(p.headset)] : nullptr;
    std::wstring error;
    if (!p.engine.start(p.mics[size_t(p.mic)], p.outs[size_t(p.cable)], headset, error)) {
        p.error = error;
        return;
    }

    // CABLE-A Output devient le micro par défaut : Discord / WhatsApp réglés sur « Par défaut » le prennent seuls.
    p.cableCapture = cableCaptureEndpoint();
    if (!p.cableCapture.empty()) {
        const std::wstring& micEndpoint = p.mics[size_t(p.mic)].endpoint;
        for (int r = 0; r < 3; ++r) {
            std::wstring current = getDefaultDevice(true, r);
            p.previous[r] = (current.empty() || sameText(current, p.cableCapture)) ? micEndpoint : current;
        }
        iniWrite(kRestore, L"cable", p.cableCapture);
        for (int r = 0; r < 3; ++r) iniWrite(kRestore, (L"role" + std::to_wstring(r)).c_str(), p.previous[r]);
        for (int r = 0; r < 3; ++r) setDefaultDevice(p.cableCapture, r);
        p.switched = true;
    }
}

void updateUi() {
    bool running = p.engine.running();
    setText(p.toggle, running ? L"Désactiver la voix" : L"Activer la voix");
    setText(p.settings, p.settingsShown ? L"Réglages ▴" : L"Réglages ▾");
    std::wstring text;
    if (!p.error.empty()) {
        text = p.error;
    } else if (running) {
        text = p.switched ? L"● Actif — dans Discord, WhatsApp…, le micro est « " + cableCaptureName() +
                                L" » (il est déjà le micro par défaut de Windows)."
                          : L"● Actif — dans Discord, WhatsApp…, choisis comme micro la sortie de ton câble virtuel.";
    } else {
        text = L"Voix désactivée — ton micro fonctionne normalement, sans effet.";
    }
    setText(p.status, text);
}

void toggleVoice() {
    if (p.engine.running()) {
        stopVoice();
        p.error.clear();
    } else {
        startVoice();
    }
    updateUi();
}

}  // namespace

// ---------------------------------------------------------------- interface publique

void create() {
    const wchar_t* modeNames[3] = {L"Sans effet", L"Autotune", L"Vocodeur"};
    for (int i = 0; i < 3; ++i) {
        p.modes[i] = theme::button(modeNames[i], theme::Kind::kSegment, IDC_MODE_NONE + i);
    }
    p.note = makeControl(L"STATIC", L"—", SS_CENTER | SS_CENTERIMAGE);
    p.caption = makeControl(L"STATIC", L"note chantée  →  note envoyée", SS_CENTER);
    for (int r = 0; r < kRowCount; ++r) {
        p.rowLabel[r] = makeControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE);
        bool stepped = r == kRowKey || r == kRowScale || r == kRowTranspose;  // crans visibles
        p.rowSlider[r] = makeSlider(kRows[r].minValue, kRows[r].maxValue, stepped ? TBS_AUTOTICKS : TBS_NOTICKS);
    }
    p.levelLabel = makeControl(L"STATIC", L"Niveau du micro", SS_LEFT | SS_CENTERIMAGE);
    p.level = theme::meter();
    SendMessageW(p.level, PBM_SETRANGE32, 0, 100);
    p.chords = theme::button(L"Accords (chœur de robots, vocodeur)", theme::Kind::kToggle, IDC_CHORDS);
    p.monitor = theme::button(L"Entendre ma voix (écho)", theme::Kind::kToggle, IDC_MONITOR);
    p.presetLabel = makeControl(L"STATIC", L"Préréglages :", SS_LEFT | SS_CENTERIMAGE);
    const wchar_t* presetNames[4] = {L"Naturel", L"T-Pain", L"Robot", L"Chœur"};
    for (int i = 0; i < 4; ++i)
        p.presets[i] = theme::button(presetNames[i], theme::Kind::kButton, IDC_PRESET_NATURAL + i);
    p.toggle = theme::button(L"Activer la voix", theme::Kind::kButton, IDC_VTOGGLE);
    p.settings = theme::button(L"Réglages", theme::Kind::kButton, IDC_VSETTINGS);
    p.status = makeControl(L"STATIC", L"", SS_LEFT);
    p.micLabel = makeControl(L"STATIC", L"Micro :", SS_LEFT);
    p.micCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_MIC);
    p.cableLabel = makeControl(L"STATIC", L"Câble vers les applis (le micro virtuel) :", SS_LEFT);
    p.cableCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_CABLE);
    p.headsetLabel = makeControl(L"STATIC", L"Retour casque :", SS_LEFT);
    p.headsetCombo = makeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_HEADSET);
    for (HWND combo : {p.micCombo, p.cableCombo, p.headsetCombo}) theme::darkControl(combo);
    theme::slider(p.rowSlider[kRowTranspose], true);  // hauteur : remplie depuis le 0 central
}

int layout(int top, bool shown) {
    p.shown = shown;
    const int margin = 20, width = ui.width, inner = width - 2 * margin;
    int y = top + 4;
    const int modeW = (inner - 2 * 9) / 3;  // 114 à la largeur de départ
    for (int i = 0; i < 3; ++i) place(p.modes[i], margin + i * (modeW + 9), y, modeW, 30, ui.font);
    y += 40;
    place(p.note, margin, y, inner, 36, ui.bigFont);
    y += 36;
    place(p.caption, margin, y, inner, 18, ui.smallFont);
    y += 26;
    for (int r = 0; r < kRowCount; ++r) {
        place(p.rowLabel[r], margin, y, 140, 30, ui.smallFont);
        place(p.rowSlider[r], margin + 140, y, inner - 140, 30, ui.font);
        y += 32;
    }
    place(p.levelLabel, margin, y, 140, 16, ui.smallFont);
    place(p.level, margin + 148, y + 4, inner - 156, 8, ui.smallFont);
    y += 26;
    place(p.chords, margin, y, inner, 22, ui.smallFont);
    y += 24;
    place(p.monitor, margin, y, inner, 22, ui.smallFont);
    y += 32;
    place(p.presetLabel, margin, y, 86, 28, ui.smallFont);
    const int presetW = (inner - 88 - 3 * 4) / 4;  // 64 à la largeur de départ
    for (int i = 0; i < 4; ++i) place(p.presets[i], margin + 88 + i * (presetW + 4), y, presetW, 28, ui.smallFont);
    y += 40;
    place(p.toggle, margin, y, 200, 32, ui.font);
    place(p.settings, width - margin - 120, y, 120, 32, ui.smallFont);
    y += 42;
    place(p.status, margin, y, inner, 36, ui.smallFont);
    y += 42;

    HWND all[] = {p.modes[0], p.modes[1], p.modes[2], p.note, p.caption, p.levelLabel, p.level, p.chords, p.monitor,
                  p.presetLabel, p.presets[0], p.presets[1], p.presets[2], p.presets[3], p.toggle, p.settings, p.status};
    for (HWND h : all) ShowWindow(h, shown ? SW_SHOW : SW_HIDE);
    for (int r = 0; r < kRowCount; ++r) {
        ShowWindow(p.rowLabel[r], shown ? SW_SHOW : SW_HIDE);
        ShowWindow(p.rowSlider[r], shown ? SW_SHOW : SW_HIDE);
    }

    bool extra = shown && p.settingsShown;
    HWND settingsControls[] = {p.micLabel, p.micCombo, p.cableLabel, p.cableCombo, p.headsetLabel, p.headsetCombo};
    for (HWND h : settingsControls) ShowWindow(h, extra ? SW_SHOW : SW_HIDE);
    if (p.settingsShown) {
        place(p.micLabel, margin, y, inner, 18, ui.smallFont);
        place(p.micCombo, margin, y + 18, inner, 240, ui.smallFont);
        place(p.cableLabel, margin, y + 50, inner, 18, ui.smallFont);
        place(p.cableCombo, margin, y + 68, inner, 240, ui.smallFont);
        place(p.headsetLabel, margin, y + 100, inner, 18, ui.smallFont);
        place(p.headsetCombo, margin, y + 118, inner, 240, ui.smallFont);
        y += 154;
    }
    return y;
}

bool onCommand(WPARAM wp, LPARAM) {
    int id = LOWORD(wp);
    switch (id) {
    case IDC_MODE_NONE:
    case IDC_MODE_TUNE:
    case IDC_MODE_VOCODER:
        applyMode(id - IDC_MODE_NONE, true);
        return true;
    case IDC_CHORDS:
    case IDC_MONITOR:
        applyChecks(true);
        return true;
    case IDC_PRESET_NATURAL:
    case IDC_PRESET_TPAIN:
    case IDC_PRESET_ROBOT:
    case IDC_PRESET_CHOIR:
        applyPreset(id);
        return true;
    case IDC_VTOGGLE:
        toggleVoice();
        return true;
    case IDC_VSETTINGS:
        p.settingsShown = !p.settingsShown;
        PostMessageW(ui.wnd, WM_APP_RELAYOUT, 0, 0);
        updateUi();
        return true;
    case IDC_MIC:
    case IDC_CABLE:
    case IDC_HEADSET:
        if (HIWORD(wp) == CBN_SELCHANGE) {
            HWND combo = id == IDC_MIC ? p.micCombo : id == IDC_CABLE ? p.cableCombo : p.headsetCombo;
            const auto& list = id == IDC_MIC ? p.mics : p.outs;
            int index = int(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            if (index >= 0 && index < int(list.size())) {
                iniWrite(kSection, id == IDC_MIC ? L"micro" : id == IDC_CABLE ? L"cable" : L"retour",
                         list[size_t(index)].endpoint);
                if (p.engine.running()) startVoice();
                else refreshDevices();
                updateUi();
                // Nouveau câble pour la voix : la musique doit en prendre un autre si c'était le sien.
                if (id == IDC_CABLE) PostMessageW(ui.wnd, WM_APP_CABLES_CHANGED, 0, 0);
            }
        }
        return true;
    }
    return false;
}

bool onScroll(HWND slider) {
    for (int r = 0; r < kRowCount; ++r) {
        if (slider == p.rowSlider[r]) {
            applyRow(r, true);
            return true;
        }
    }
    return false;
}

bool isStatus(HWND h, bool& error) {
    // Libellés secondaires (gris) : comme un texte d'état sans erreur.
    if (h == p.caption || h == p.levelLabel || h == p.presetLabel || h == p.micLabel || h == p.cableLabel ||
        h == p.headsetLabel) {
        error = false;
        return true;
    }
    for (HWND label : p.rowLabel)
        if (h == label) {
            error = false;
            return true;
        }
    if (h != p.status) return false;
    error = !p.error.empty();
    return true;
}

void onTimer() {
    if (!p.shown) return;
    if (!p.engine.running()) {
        setText(p.note, L"—");
        SendMessageW(p.level, PBM_SETPOS, 0, 0);
        return;
    }
    float detected = p.engine.detectedMidi();
    float target = p.engine.targetMidi();
    if (p.mode == voice::kModeNone) {
        setText(p.note, detected > 0 ? withCents(detected) : L"—");
    } else {
        setText(p.note, detected > 0 ? withCents(detected) + L"  →  " + withCents(target) : L"—");
    }
    float level = p.engine.level();
    float db = level > 1e-5f ? 20 * std::log10(level) : -100;
    SendMessageW(p.level, PBM_SETPOS, WPARAM(std::clamp(int((db + 60) * 100 / 60), 0, 100)), 0);
}

void onDeviceLost() {
    stopVoice();
    p.error = L"Le micro ou le câble a disparu (débranché ?). Voix arrêtée — clique sur Activer pour relancer.";
    updateUi();
}

void startup() {
    std::wstring error;
    p.engineReady = p.engine.init(error);
    if (!p.engineReady) p.error = error;
    p.engine.setLostCallback([](void*) { PostMessageW(ui.wnd, WM_APP_VOICE_LOST, 0, 0); }, nullptr);
    p.engine.setSoundMixer(soundpage::cableMixer());  // les sons du soundboard partent avec la voix

    // Réparation si la dernière session s'est mal terminée : on remet le micro par défaut d'avant.
    std::wstring cable = iniRead(kRestore, L"cable");
    if (!cable.empty()) {
        std::wstring previous[3];
        for (int r = 0; r < 3; ++r) previous[r] = iniRead(kRestore, (L"role" + std::to_wstring(r)).c_str());
        restoreDefaults(cable, previous);
        iniDeleteSection(kRestore);
    }

    // Réglages de la dernière fois.
    for (int r = 0; r < kRowCount; ++r) {
        setSliderPos(p.rowSlider[r], iniReadInt(kSection, kRows[r].key, kRows[r].defaultValue));
        applyRow(r, false);
    }
    SendMessageW(p.chords, BM_SETCHECK, iniReadInt(kSection, L"accords", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(p.monitor, BM_SETCHECK, iniReadInt(kSection, L"casque", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    applyChecks(false);
    applyMode(iniReadInt(kSection, L"mode", voice::kModeAutotune), false);

    if (p.engineReady) {
        refreshDevices();
        startVoice();  // la voix démarre toujours avec SoundZ (le micro par défaut passe sur le câble)
    }
    updateUi();
}

std::vector<OutputDevice> microphones() { return p.engineReady ? p.engine.microphones() : std::vector<OutputDevice>(); }

std::wstring currentMicName() {
    return p.mic >= 0 && p.mic < int(p.mics.size()) ? shortDeviceName(p.mics[size_t(p.mic)].name) : L"(aucun)";
}

void reloadDevices() {
    if (!p.engineReady) return;
    if (p.engine.running() || !p.error.empty()) startVoice();  // remet d'abord le micro par défaut d'origine, puis relit les choix
    else refreshDevices();
    updateUi();
}

std::wstring deviceReport() {
    std::wstring error;
    if (!p.engineReady) p.engineReady = p.engine.init(error);
    if (!p.engineReady) return L"voix : son de Windows inaccessible\r\n";
    refreshDevices();
    auto name = [](const std::vector<OutputDevice>& list, int i) {
        return i >= 0 ? list[size_t(i)].name : std::wstring(L"(aucun)");
    };
    std::wstring defaultMic = getDefaultDevice(true, 0), defaultName = L"(aucun)";
    for (const auto& m : p.mics)
        if (sameText(m.endpoint, defaultMic)) defaultName = m.name;
    return L"micro par défaut de Windows : " + defaultName + L"\r\nmicro choisi : " + name(p.mics, p.mic) +
           L"\r\ncâble vers les applis : " + name(p.outs, p.cable) + L"\r\nretour casque : " + name(p.outs, p.headset) + L"\r\n";
}

bool running() { return p.engine.running(); }

void shutdown() { stopVoice(); }

// ---------------------------------------------------------------- télécommande du téléphone

std::string remoteJson() {
    auto flag = [](bool b) { return std::string(b ? "true" : "false"); };
    const bool on = p.engine.running();
    std::string rows = "[";
    for (int r = 0; r < kRowCount; ++r) {
        const int v = sliderPos(p.rowSlider[r]);
        if (r) rows += ",";
        rows += "{\"label\":" + jsonText(rowText(r, v)) + ",\"min\":" + std::to_string(kRows[r].minValue) +
                ",\"max\":" + std::to_string(kRows[r].maxValue) + ",\"value\":" + std::to_string(v) + "}";
    }
    rows += "]";
    std::wstring note = L"—";
    int level = 0;
    if (on) {
        const float detected = p.engine.detectedMidi(), target = p.engine.targetMidi();
        if (detected > 0) note = p.mode == voice::kModeNone ? withCents(detected) : withCents(detected) + L"  →  " + withCents(target);
        const float amplitude = p.engine.level();
        const float db = amplitude > 1e-5f ? 20 * std::log10(amplitude) : -100;
        level = std::clamp(int((db + 60) * 100 / 60), 0, 100);
    }
    wchar_t status[512] = {}, monitor[256] = {};
    GetWindowTextW(p.status, status, 512);
    GetWindowTextW(p.monitor, monitor, 256);
    return "{\"on\":" + flag(on) + ",\"status\":" + jsonText(status) + ",\"err\":" + flag(!p.error.empty()) +
           ",\"mode\":" + std::to_string(p.mode) + ",\"rows\":" + rows +
           ",\"chords\":" + flag(SendMessageW(p.chords, BM_GETCHECK, 0, 0) == BST_CHECKED) +
           ",\"monitor\":" + flag(SendMessageW(p.monitor, BM_GETCHECK, 0, 0) == BST_CHECKED) + ",\"monitorLabel\":" + jsonText(monitor) +
           ",\"note\":" + jsonText(note) + ",\"level\":" + std::to_string(level) + "}";
}

// Mêmes effets qu'un clic dans l'onglet Voix : les contrôles du PC suivent.
void remoteCommand(const std::string& action, int a, int b) {
    if (action == "toggle") {
        toggleVoice();
    } else if (action == "mode") {
        applyMode(a, true);
    } else if (action == "row" && a >= 0 && a < kRowCount) {
        setRow(a, std::clamp(b, kRows[a].minValue, kRows[a].maxValue));
    } else if (action == "chords" || action == "monitor") {
        HWND h = action == "chords" ? p.chords : p.monitor;
        SendMessageW(h, BM_SETCHECK, a ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(h, nullptr, FALSE);
        applyChecks(true);
    } else if (action == "preset" && a >= 0 && a < 4) {
        applyPreset(IDC_PRESET_NATURAL + a);
        InvalidateRect(p.chords, nullptr, FALSE);
    }
    updateUi();
}

}  // namespace voicepage
