#include "overlay.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "sound_dsp.h"
#include "sound_image.h"
#include "sound_page.h"
#include "theme.h"
#include "ui.h"
#include "util.h"

namespace overlay {
namespace {

constexpr wchar_t kClass[] = L"SoundZOverlay";
constexpr wchar_t kToastClass[] = L"SoundZToast";
constexpr wchar_t kSection[] = L"overlay";
constexpr UINT_PTR kDragTimer = 1;

// Dimensions en DIP.
constexpr int kColumns = 6, kTileW = 62, kTileH = 86, kGap = 6, kPad = 12, kImage = 34;
constexpr int kWidth = 2 * kPad + kColumns * kTileW + (kColumns - 1) * kGap;
constexpr int kHeader = 40;
constexpr size_t kMinSelection = sound::kRate / 10;  // 0,1 s
constexpr DWORD kToastMs = 2600, kNoticeMs = 6000;

enum class View { kBoard, kTrim };
enum class Part { kNone, kHeader, kClose, kStop, kSound, kCapture, kWave, kListen, kVolume, kCleaning, kKeep, kDiscard, kBack };
enum Drag { kNoDrag, kMoveWindow, kHandleStart, kHandleEnd, kNewSelection, kVolumeDrag, kCleaningDrag };

struct Hit {
    Part part = Part::kNone;
    int index = -1;
    bool operator==(const Hit& o) const { return part == o.part && index == o.index; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};

struct Tile {
    int uid = 0, tag = 0;
    std::wstring name, hotkey;
    bool unnamed = false;
    HBITMAP bitmap = nullptr;
};

// Capture « à trier » en cours de découpage.
struct Trim {
    std::wstring file, time;
    std::vector<float> raw;  // mono, 48 kHz
    size_t selStart = 0, selEnd = 0;
    std::vector<float> peaks;
    int peaksWidth = 0;
    int volume = 100, cleaning = 60;  // %
    soundpage::BoardSamples cleaned;  // le passage choisi, nettoyé : ce qui sera gardé
    std::wstring message;
    bool error = false;
};

// Positions (pixels de la fenêtre), recalculées à chaque changement.
struct Layout {
    int width = 0, height = 0;
    RECT header{}, close{}, stop{}, status{}, empty{};
    std::vector<RECT> tiles;  // sons, puis captures
    RECT title{}, wave{}, selLabel{}, listen{}, volumeLabel{}, volume{}, cleaningLabel{}, cleaning{}, message{};
    RECT keep{}, discard{}, back{}, note{};
};

struct State {
    HWND wnd = nullptr, toast = nullptr;
    bool shown = false;
    UINT dpi = 96;
    HFONT font = nullptr, bold = nullptr, small = nullptr, tiny = nullptr;
    View view = View::kBoard;
    std::vector<Tile> tiles;
    std::vector<soundpage::BoardCapture> captures;
    soundimg::Image scissors;
    HBITMAP scissorsBitmap = nullptr;
    uint32_t version = 0;
    uint64_t playing = 0;
    bool voice = false, hear = false;
    Layout lay;
    Hit hover, pressed;
    bool tracking = false;  // WM_MOUSELEAVE demandé
    int drag = kNoDrag;
    POINT grab{};  // déplacement de la fenêtre : écart entre le curseur et le coin de la fenêtre
    size_t anchor = 0, dragStart = 0, dragEnd = 0;
    int dragValue = 0;
    Trim trim;
    std::wstring notice;
    bool noticeError = false;
    DWORD noticeTick = 0, topmostTick = 0;
    // La confirmation en haut de l'écran.
    std::wstring toastText;
    bool toastError = false;
    DWORD toastTick = 0;
    UINT toastDpi = 0;
    HFONT toastFont = nullptr;
};

State s;

int D(int dip) { return MulDiv(dip, int(s.dpi), 96); }

RECT rc(int l, int t, int r, int b) { return RECT{D(l), D(t), D(r), D(b)}; }

bool inside(const RECT& r, POINT pt) { return PtInRect(&r, pt) != FALSE; }

UINT monitorDpi(HMONITOR monitor) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static Fn fn = [] {
        HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return shcore ? reinterpret_cast<Fn>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    }();
    UINT x = 96, y = 96;
    if (fn && SUCCEEDED(fn(monitor, 0 /* MDT_EFFECTIVE_DPI */, &x, &y))) return x;
    return GetDpiForSystem();
}

HFONT makeFont(UINT dpi, int points, int weight) {
    NONCLIENTMETRICSW m = {};
    m.cbSize = sizeof(m);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(m), &m, 0, dpi);
    LOGFONTW f = m.lfMessageFont;
    if (points) f.lfHeight = -MulDiv(points, int(dpi), 72);
    if (weight) f.lfWeight = weight;
    return CreateFontIndirectW(&f);
}

void makeFonts() {
    for (HFONT* f : {&s.font, &s.bold, &s.small, &s.tiny})
        if (*f) DeleteObject(*f);
    s.font = makeFont(s.dpi, 10, 0);
    s.bold = makeFont(s.dpi, 10, FW_SEMIBOLD);
    s.small = makeFont(s.dpi, 0, 0);  // police des messages de Windows (9 pt)
    s.tiny = makeFont(s.dpi, 7, 0);
}

int primaryButton() { return GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON; }
bool buttonDown() { return (GetAsyncKeyState(primaryButton()) & 0x8000) != 0; }

std::wstring secondsText(size_t frames) {
    wchar_t b[32];
    swprintf(b, 32, L"%.1f s", double(frames) / sound::kRate);
    std::wstring t = b;
    std::replace(t.begin(), t.end(), L'.', L',');
    return t;
}

// ---------------------------------------------------------------- disposition

void computeLayout() {
    Layout& l = s.lay;
    l = Layout();
    l.width = D(kWidth);
    l.header = rc(0, 0, kWidth, kHeader);
    l.close = rc(kWidth - kPad - 30, 8, kWidth - kPad, 32);
    l.stop = rc(kWidth - kPad - 30 - 6 - 74, 8, kWidth - kPad - 30 - 6, 32);
    int y = kHeader + 2;
    if (s.view == View::kBoard) {
        const int count = int(s.tiles.size() + s.captures.size());
        for (int i = 0; i < count; ++i) {
            int x = kPad + (i % kColumns) * (kTileW + kGap), top = y + (i / kColumns) * (kTileH + kGap);
            l.tiles.push_back(rc(x, top, x + kTileW, top + kTileH));
        }
        if (count == 0) {
            l.empty = rc(kPad, y, kWidth - kPad, y + 56);
            y += 60;
        } else {
            y += ((count + kColumns - 1) / kColumns) * (kTileH + kGap) + 2;
        }
        l.status = rc(kPad, y, kWidth - kPad, y + 34);
        y += 40;
    } else {
        l.title = rc(kPad, y, kWidth - kPad, y + 22);
        y += 26;
        l.wave = rc(kPad, y, kWidth - kPad, y + 76);
        y += 82;
        l.selLabel = rc(kPad, y, kWidth - kPad - 120, y + 28);
        l.listen = rc(kWidth - kPad - 112, y, kWidth - kPad, y + 28);
        y += 36;
        l.volumeLabel = rc(kPad, y, kPad + 130, y + 26);
        l.volume = rc(kPad + 134, y, kWidth - kPad, y + 26);
        y += 30;
        l.cleaningLabel = rc(kPad, y, kPad + 130, y + 26);
        l.cleaning = rc(kPad + 134, y, kWidth - kPad, y + 26);
        y += 32;
        l.message = rc(kPad, y, kWidth - kPad, y + 34);
        y += 38;
        l.keep = rc(kPad, y, kPad + 150, y + 32);
        l.discard = rc(kPad + 158, y, kPad + 158 + 112, y + 32);
        l.back = rc(kWidth - kPad - 100, y, kWidth - kPad, y + 32);
        y += 38;
        l.note = rc(kPad, y, kWidth - kPad, y + 20);
        y += 26;
    }
    l.height = D(y);
}

// Taille de la fenêtre ajustée au contenu, sans sortir de l'écran.
void fit() {
    computeLayout();
    if (!s.wnd) return;
    RECT w;
    GetWindowRect(s.wnd, &w);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(s.wnd, MONITOR_DEFAULTTONEAREST), &mi);
    int x = w.left, y = w.top;
    if (y + s.lay.height > mi.rcWork.bottom) y = std::max(int(mi.rcWork.top), int(mi.rcWork.bottom) - s.lay.height);
    if (x + s.lay.width > mi.rcWork.right) x = std::max(int(mi.rcWork.left), int(mi.rcWork.right) - s.lay.width);
    SetWindowPos(s.wnd, nullptr, x, y, s.lay.width, s.lay.height, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(s.wnd, nullptr, FALSE);
}

void freeBitmaps() {
    for (Tile& t : s.tiles)
        if (t.bitmap) DeleteObject(t.bitmap);
    s.tiles.clear();
    if (s.scissorsBitmap) DeleteObject(s.scissorsBitmap);
    s.scissorsBitmap = nullptr;
}

// Les sons et les captures ont changé (ou la taille de l'écran) : on reprend la liste.
void rebuild() {
    freeBitmaps();
    const int px = D(kImage);
    for (const soundpage::BoardSound& b : soundpage::boardSounds()) {
        Tile t;
        t.uid = b.uid;
        t.tag = b.tag;
        t.name = b.name;
        t.hotkey = b.hotkey;
        t.unnamed = b.unnamed;
        t.bitmap = b.image ? soundimg::makeBitmap(*b.image, px) : nullptr;
        s.tiles.push_back(std::move(t));
    }
    s.captures = soundpage::boardCaptures();
    if (s.scissors.empty()) soundimg::renderText(L"✂️", s.scissors);
    s.scissorsBitmap = soundimg::makeBitmap(s.scissors, px * 4 / 5);
    s.version = soundpage::boardVersion();
    // La capture en cours de découpage a été rangée ou supprimée depuis le bureau.
    if (s.view == View::kTrim) {
        bool still = false;
        for (const soundpage::BoardCapture& c : s.captures) still = still || sameText(c.file, s.trim.file);
        if (!still) {
            s.view = View::kBoard;
            s.trim = Trim();
        }
    }
    s.hover = s.pressed = Hit();
    fit();
}

// ---------------------------------------------------------------- découpage d'une capture

void processTrim() {
    Trim& t = s.trim;
    t.selEnd = std::min(t.selEnd, t.raw.size());
    t.selStart = std::min(t.selStart, t.selEnd);
    std::vector<float> part(t.raw.begin() + long(t.selStart), t.raw.begin() + long(t.selEnd));
    sound::CleanResult r = sound::clean(std::move(part), float(t.cleaning) / 100.0f);
    if (r.empty) {
        t.cleaned.reset();
        t.message = L"Ce passage est silencieux : choisis-en un autre avec les poignées.";
        t.error = true;
    } else {
        t.cleaned = std::make_shared<const std::vector<float>>(std::move(r.samples));
        t.message = L"Glisse les poignées pour garder le bon passage (ou trace-le), puis ▶ Écouter.";
        t.error = false;
    }
}

void openTrim(int index) {
    if (index < 0 || index >= int(s.captures.size())) return;
    Trim t;
    t.file = s.captures[size_t(index)].file;
    t.time = s.captures[size_t(index)].time;
    if (!sound::readWav(t.file, t.raw) || t.raw.empty()) {
        s.notice = L"Cette capture ne peut pas être lue.";
        s.noticeError = true;
        s.noticeTick = GetTickCount();
        InvalidateRect(s.wnd, nullptr, FALSE);
        return;
    }
    // Comme sur le bureau : on propose les dernières secondes (durée de capture habituelle).
    const size_t last = size_t(soundpage::defaultSeconds()) * sound::kRate;
    t.selEnd = t.raw.size();
    t.selStart = t.raw.size() > last ? t.raw.size() - last : 0;
    t.cleaning = soundpage::defaultCleaning();
    s.trim = std::move(t);
    s.view = View::kTrim;
    processTrim();
    s.hover = s.pressed = Hit();
    fit();
}

void backToBoard(const std::wstring& notice, bool error) {
    s.view = View::kBoard;
    s.trim = Trim();
    if (!notice.empty()) {
        s.notice = notice;
        s.noticeError = error;
        s.noticeTick = GetTickCount();
    }
    s.hover = s.pressed = Hit();
    fit();
}

void keepTrim() {
    Trim& t = s.trim;
    std::wstring error = soundpage::keepCapture(t.file, t.raw, t.selStart, t.selEnd, t.volume, t.cleaning, t.cleaned);
    if (!error.empty()) {
        t.message = error;
        t.error = true;
        InvalidateRect(s.wnd, nullptr, FALSE);
        return;
    }
    backToBoard(L"✓ Rangé dans le soundboard. Nom et image : plus tard, dans SoundZ sur le bureau.", false);
    rebuild();  // le nouveau son apparaît
}

// ---------------------------------------------------------------- forme du son

int sampleToX(size_t sample) {
    const RECT& r = s.lay.wave;
    const int left = r.left + D(6), width = r.right - r.left - D(12);
    size_t n = std::max<size_t>(1, s.trim.raw.size());
    return left + int(double(sample) / double(n) * width);
}

size_t xToSample(int x) {
    const RECT& r = s.lay.wave;
    const int left = r.left + D(6), width = std::max(1, int(r.right - r.left) - D(12));
    double t = std::clamp(double(x - left) / width, 0.0, 1.0);
    return size_t(t * double(s.trim.raw.size()));
}

int handleAt(int x) {
    int xs = sampleToX(s.trim.selStart), xe = sampleToX(s.trim.selEnd), grab = D(8);
    if (std::abs(x - xe) <= grab && std::abs(x - xe) <= std::abs(x - xs)) return kHandleEnd;
    if (std::abs(x - xs) <= grab) return kHandleStart;
    return kNoDrag;
}

// ---------------------------------------------------------------- curseurs (volume, nettoyage)

int sliderValue(const RECT& r, int x, int maxValue, int step) {
    const int left = r.left + D(8), width = std::max(1, int(r.right - r.left) - D(16));
    double t = std::clamp(double(x - left) / width, 0.0, 1.0);
    return std::clamp(int(std::lround(t * maxValue / step)) * step, 0, maxValue);
}

// ---------------------------------------------------------------- souris

Hit hitTest(POINT pt) {
    const Layout& l = s.lay;
    if (inside(l.close, pt)) return {Part::kClose};
    if (inside(l.stop, pt)) return {Part::kStop};
    if (s.view == View::kBoard) {
        for (int i = 0; i < int(l.tiles.size()); ++i) {
            if (!inside(l.tiles[size_t(i)], pt)) continue;
            if (i < int(s.tiles.size())) return {Part::kSound, i};
            return {Part::kCapture, i - int(s.tiles.size())};
        }
    } else {
        if (inside(l.listen, pt)) return {Part::kListen};
        if (inside(l.volume, pt)) return {Part::kVolume};
        if (inside(l.cleaning, pt)) return {Part::kCleaning};
        if (inside(l.keep, pt)) return {Part::kKeep};
        if (inside(l.discard, pt)) return {Part::kDiscard};
        if (inside(l.back, pt)) return {Part::kBack};
        if (inside(l.wave, pt)) return {Part::kWave};
    }
    if (inside(l.header, pt)) return {Part::kHeader};
    return {};
}

void dragTo(POINT screen) {
    if (s.drag == kMoveWindow) {
        SetWindowPos(s.wnd, nullptr, screen.x - s.grab.x, screen.y - s.grab.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    POINT pt = screen;
    ScreenToClient(s.wnd, &pt);
    Trim& t = s.trim;
    switch (s.drag) {
    case kHandleStart: t.selStart = std::min(xToSample(pt.x), t.selEnd > kMinSelection ? t.selEnd - kMinSelection : 0); break;
    case kHandleEnd: t.selEnd = std::min(t.raw.size(), std::max(xToSample(pt.x), t.selStart + kMinSelection)); break;
    case kNewSelection: {
        size_t at = xToSample(pt.x);
        t.selStart = std::min(s.anchor, at);
        t.selEnd = std::max(s.anchor, at);
        break;
    }
    case kVolumeDrag: t.volume = sliderValue(s.lay.volume, pt.x, 200, 5); break;
    case kCleaningDrag: t.cleaning = sliderValue(s.lay.cleaning, pt.x, 100, 5); break;
    default: return;
    }
    InvalidateRect(s.wnd, nullptr, FALSE);
}

// Glisser à la souris : suivi par un minuteur (la fenêtre ne prend pas la main, donc pas de SetCapture).
void beginDrag(int kind, POINT client) {
    s.drag = kind;
    POINT screen = client;
    ClientToScreen(s.wnd, &screen);
    if (kind == kMoveWindow) {
        RECT w;
        GetWindowRect(s.wnd, &w);
        s.grab = {screen.x - w.left, screen.y - w.top};
    }
    s.dragStart = s.trim.selStart;
    s.dragEnd = s.trim.selEnd;
    s.dragValue = kind == kCleaningDrag ? s.trim.cleaning : s.trim.volume;
    if (kind == kNewSelection) s.anchor = xToSample(client.x);
    SetTimer(s.wnd, kDragTimer, 15, nullptr);
    dragTo(screen);
}

void endDrag() {
    if (s.drag == kNoDrag) return;
    const int kind = s.drag;
    s.drag = kNoDrag;
    KillTimer(s.wnd, kDragTimer);
    Trim& t = s.trim;
    if (kind == kMoveWindow) {
        RECT w;
        GetWindowRect(s.wnd, &w);
        iniWrite(kSection, L"x", std::to_wstring(w.left));
        iniWrite(kSection, L"y", std::to_wstring(w.top));
    } else if (kind == kHandleStart || kind == kHandleEnd || kind == kNewSelection) {
        if (t.selEnd < t.selStart + kMinSelection) {  // simple clic : on garde la sélection d'avant
            t.selStart = s.dragStart;
            t.selEnd = s.dragEnd;
        }
        if (t.selStart != s.dragStart || t.selEnd != s.dragEnd) {
            soundpage::stopPreview();
            processTrim();
        }
    } else if (kind == kCleaningDrag && t.cleaning != s.dragValue) {
        iniWrite(L"soundboard", L"nettoyage", std::to_wstring(t.cleaning));
        soundpage::stopPreview();
        processTrim();
    }
    InvalidateRect(s.wnd, nullptr, FALSE);
}

void click(const Hit& h) {
    switch (h.part) {
    case Part::kClose: hide(); break;
    case Part::kStop: soundpage::stopSounds(); break;
    case Part::kSound:
        if (h.index < int(s.tiles.size())) soundpage::playUid(s.tiles[size_t(h.index)].uid);
        break;
    case Part::kCapture: openTrim(h.index); break;
    case Part::kListen:
        if (s.trim.cleaned) soundpage::preview(s.trim.cleaned, float(s.trim.volume) / 100.0f);
        break;
    case Part::kKeep: keepTrim(); break;
    case Part::kDiscard:
        soundpage::stopPreview();
        soundpage::discardCapture(s.trim.file);
        backToBoard(L"Capture supprimée.", false);
        rebuild();
        break;
    case Part::kBack: backToBoard(L"", false); break;
    default: break;
    }
}

// ---------------------------------------------------------------- dessin

void drawText(HDC dc, const std::wstring& t, RECT r, HFONT f, COLORREF c, UINT format) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), -1, &r, format | DT_NOPREFIX);
    SelectObject(dc, old);
}

void drawButton(HDC dc, const RECT& r, const std::wstring& label, Part part, bool primary, bool enabled = true) {
    const bool hover = enabled && s.hover.part == part, pressed = enabled && s.pressed.part == part && hover;
    COLORREF fill = primary ? (pressed ? theme::kAccentBright : hover ? theme::kAccentHover : theme::kAccent)
                            : (pressed ? theme::kSurfacePressed : hover ? theme::kButtonHover : theme::kButton);
    if (!enabled) fill = theme::kSurface;
    theme::roundRect(dc, r, float(D(6)), fill);
    drawText(dc, label, r, s.small, enabled ? theme::kText : theme::kDisabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void drawSlider(HDC dc, const RECT& r, int value, int maxValue, Part part) {
    const int left = r.left + D(8), right = r.right - D(8), mid = (r.top + r.bottom) / 2;
    const int x = left + int(double(value) / maxValue * (right - left));
    RECT track = {left, mid - D(2), right, mid + D(2)};
    theme::roundRect(dc, track, float(D(2)), theme::kBorder);
    RECT filled = {left, mid - D(2), std::max(left + D(2), x), mid + D(2)};
    theme::roundRect(dc, filled, float(D(2)), theme::kAccentBright);
    const bool active = s.hover.part == part || (part == Part::kVolume ? s.drag == kVolumeDrag : s.drag == kCleaningDrag);
    theme::circle(dc, float(x), float(mid), float(D(active ? 8 : 7)), RGB(255, 255, 255));
}

void drawTile(HDC dc, int i) {
    const RECT& r = s.lay.tiles[size_t(i)];
    const bool isSound = i < int(s.tiles.size());
    const Part part = isSound ? Part::kSound : Part::kCapture;
    const int index = isSound ? i : i - int(s.tiles.size());
    const bool hover = s.hover.part == part && s.hover.index == index;
    const bool pressed = hover && s.pressed.part == part && s.pressed.index == index;
    const int w = r.right - r.left, px = D(kImage);
    if (isSound) {
        const Tile& t = s.tiles[size_t(i)];
        const bool playing = (s.playing >> (t.tag & 63)) & 1;
        COLORREF fill = playing ? theme::kAccentSoft : pressed ? theme::kSurfacePressed : hover ? theme::kSurfaceHover : theme::kSurface;
        theme::roundRect(dc, r, float(D(9)), fill);
        if (playing) theme::roundFrame(dc, r, float(D(9)), theme::kAccentBright, 1.5f);
        if (t.bitmap) soundimg::draw(dc, t.bitmap, r.left + (w - px) / 2 + (pressed ? 1 : 0), r.top + D(6) + (pressed ? 1 : 0), px);
        RECT name = {r.left + D(3), r.top + D(6) + px + D(3), r.right - D(3), r.bottom - D(14)};
        drawText(dc, t.unnamed ? L"✎ " + t.name : t.name, name, s.small, t.unnamed ? theme::kAmber : theme::kText,
                 DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
        if (!t.hotkey.empty()) {
            RECT key = {r.left + D(2), r.bottom - D(15), r.right - D(2), r.bottom - D(3)};
            drawText(dc, t.hotkey, key, s.tiny, theme::kFaint, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    } else {
        const soundpage::BoardCapture& c = s.captures[size_t(index)];
        theme::roundRect(dc, r, float(D(9)), pressed ? theme::kSurfacePressed : hover ? theme::kAmberSoft : theme::kSurface);
        theme::roundFrame(dc, r, float(D(9)), theme::kAmberLine, 1.2f, true);
        const int sp = px * 4 / 5;
        if (s.scissorsBitmap) soundimg::draw(dc, s.scissorsBitmap, r.left + (w - sp) / 2, r.top + D(9), sp);
        RECT label = {r.left + D(3), r.top + D(9) + sp + D(5), r.right - D(3), r.bottom - D(15)};
        drawText(dc, L"À trier", label, s.small, theme::kAmber, DT_CENTER | DT_SINGLELINE);
        RECT time = {r.left + D(2), r.bottom - D(16), r.right - D(2), r.bottom - D(3)};
        drawText(dc, c.time, time, s.tiny, theme::kFaint, DT_CENTER | DT_SINGLELINE);
    }
}

void drawWave(HDC dc) {
    Trim& t = s.trim;
    const RECT& box = s.lay.wave;
    theme::roundRect(dc, box, float(D(8)), theme::kSurface);
    const int left = box.left + D(6), width = box.right - box.left - D(12);
    const int top = box.top + D(6), bottom = box.bottom - D(6);
    if (t.raw.empty() || width <= 0) return;
    if (t.peaksWidth != width) {  // crête de chaque colonne de pixels, mise à l'échelle du plus fort
        t.peaks.assign(size_t(width), 0.0f);
        float peak = 1e-4f;
        for (int x = 0; x < width; ++x) {
            size_t a = size_t(double(x) / width * t.raw.size()), b = size_t(double(x + 1) / width * t.raw.size());
            float m = 0;
            for (size_t i = a; i < std::min(b, t.raw.size()); ++i) m = std::max(m, std::fabs(t.raw[i]));
            t.peaks[size_t(x)] = m;
            peak = std::max(peak, m);
        }
        for (float& v : t.peaks) v /= peak;
        t.peaksWidth = width;
    }
    const int xs = sampleToX(t.selStart), xe = sampleToX(t.selEnd);
    RECT sel = {xs, box.top + D(2), xe, box.bottom - D(2)};
    FillRect(dc, &sel, theme::brush(theme::kAccentSoft));
    const int mid = (top + bottom) / 2, half = (bottom - top) / 2;
    HPEN in = CreatePen(PS_SOLID, 1, theme::kAccentLight), out = CreatePen(PS_SOLID, 1, theme::kBorder);
    HGDIOBJ old = SelectObject(dc, out);
    for (int x = 0; x < width; ++x) {
        const int px = left + x;
        SelectObject(dc, px >= xs && px <= xe ? in : out);
        int a = std::max(1, int(t.peaks[size_t(x)] * half));
        MoveToEx(dc, px, mid - a, nullptr);
        LineTo(dc, px, mid + a + 1);
    }
    SelectObject(dc, old);
    DeleteObject(in);
    DeleteObject(out);
    for (int x : {xs, xe}) {  // les poignées : un trait et une pastille ronde en haut
        RECT bar = {x - D(1), box.top + D(2), x + D(1), box.bottom - D(2)};
        theme::roundRect(dc, bar, float(D(1)), RGB(255, 255, 255));
        theme::circle(dc, float(x), float(box.top + D(7)), float(D(5)), RGB(255, 255, 255));
    }
}

std::wstring statusText(bool& error) {
    if (!s.notice.empty()) {
        error = s.noticeError;
        return s.notice;
    }
    error = false;
    if (!s.voice) {
        error = true;
        return s.hear ? L"La voix est désactivée (onglet Voix) : toi seul entends les sons, pas ton micro."
                      : L"La voix est désactivée (onglet Voix) : les sons ne partent pas dans ton micro.";
    }
    std::wstring key = soundpage::overlayHotkeyText();
    return L"Clic sur un son : il part dans ton micro." + (key.empty() ? std::wstring() : L" " + key + L" : masquer l'overlay.");
}

void paint(HDC target) {
    const Layout& l = s.lay;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP canvas = CreateCompatibleBitmap(target, l.width, l.height);
    HGDIOBJ oldCanvas = SelectObject(dc, canvas);
    SetBkMode(dc, TRANSPARENT);
    RECT all = {0, 0, l.width, l.height};
    FillRect(dc, &all, theme::brush(theme::kBg));

    // En-tête : on l'attrape pour déplacer l'overlay.
    RECT bar = l.header;
    FillRect(dc, &bar, theme::brush(RGB(24, 25, 28)));
    theme::circle(dc, float(D(kPad + 5)), float(D(kHeader / 2)), float(D(5)), theme::kAccentBright);
    RECT title = {D(kPad + 16), 0, l.stop.left - D(6), l.header.bottom};
    drawText(dc, L"SoundZ", title, s.bold, theme::kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawButton(dc, l.stop, L"■ Stop", Part::kStop, false);
    drawButton(dc, l.close, L"✕", Part::kClose, false);

    if (s.view == View::kBoard) {
        for (int i = 0; i < int(l.tiles.size()); ++i) drawTile(dc, i);
        if (l.tiles.empty()) {
            drawText(dc,
                     L"Ton soundboard est vide. Ctrl+Maj+F12 (ou ton raccourci) garde les 10 dernières secondes du PC : "
                     L"elles apparaissent ici, à trier.",
                     l.empty, s.small, theme::kMuted, DT_LEFT | DT_WORDBREAK);
        }
        bool error = false;
        std::wstring status = statusText(error);
        drawText(dc, status, l.status, s.small, error ? theme::kDanger : theme::kMuted, DT_LEFT | DT_WORDBREAK);
    } else {
        Trim& t = s.trim;
        drawText(dc, L"✂ Capture de " + t.time, l.title, s.bold, theme::kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawWave(dc);
        std::wstring from = secondsText(t.selStart);
        from.resize(from.size() - 2);
        drawText(dc, L"Gardé : " + from + L" → " + secondsText(t.selEnd) + L"  (" + secondsText(t.selEnd - t.selStart) + L")",
                 l.selLabel, s.small, theme::kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawButton(dc, l.listen, L"▶ Écouter", Part::kListen, false, t.cleaned != nullptr);
        drawText(dc, L"Volume : " + std::to_wstring(t.volume) + L" %", l.volumeLabel, s.small, theme::kText2,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawSlider(dc, l.volume, t.volume, 200, Part::kVolume);
        drawText(dc, t.cleaning ? L"Nettoyage : " + std::to_wstring(t.cleaning) + L" %" : std::wstring(L"Nettoyage : aucun"),
                 l.cleaningLabel, s.small, theme::kText2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawSlider(dc, l.cleaning, t.cleaning, 100, Part::kCleaning);
        drawText(dc, t.message, l.message, s.small, t.error ? theme::kDanger : theme::kMuted, DT_LEFT | DT_WORDBREAK);
        drawButton(dc, l.keep, L"✓ Garder", Part::kKeep, true, t.cleaned != nullptr);
        drawButton(dc, l.discard, L"Supprimer", Part::kDiscard, false);
        drawButton(dc, l.back, L"← Retour", Part::kBack, false);
        drawText(dc, L"Le nom et l'image se choisissent ensuite dans SoundZ, sur le bureau.", l.note, s.tiny, theme::kFaint,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    FrameRect(dc, &all, theme::brush(theme::kBorder));
    BitBlt(target, 0, 0, l.width, l.height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldCanvas);
    DeleteObject(canvas);
    DeleteDC(dc);
}

void setHover(const Hit& h) {
    if (h == s.hover) return;
    s.hover = h;
    InvalidateRect(s.wnd, nullptr, FALSE);
}

bool enabledPart(const Hit& h) {
    if ((h.part == Part::kListen || h.part == Part::kKeep) && !s.trim.cleaned) return false;
    return h.part != Part::kNone && h.part != Part::kHeader && h.part != Part::kWave;
}

LRESULT CALLBACK overlayProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;  // un clic ne prend pas la main au jeu
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        paint(dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(wnd, &pt);
        Hit h = hitTest(pt);
        LPCWSTR cursor = IDC_ARROW;
        if (h.part == Part::kHeader) cursor = IDC_SIZEALL;
        if (h.part == Part::kWave) cursor = handleAt(pt.x) ? IDC_SIZEWE : IDC_IBEAM;
        if (enabledPart(h)) cursor = IDC_HAND;
        SetCursor(LoadCursorW(nullptr, cursor));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        if (!s.tracking) {
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, wnd, 0};
            s.tracking = TrackMouseEvent(&t) != FALSE;
        }
        if (s.drag == kNoDrag) setHover(hitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
        return 0;
    }
    case WM_MOUSELEAVE:
        s.tracking = false;
        if (s.drag == kNoDrag) setHover(Hit());
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        Hit h = hitTest(pt);
        s.pressed = h;
        if (h.part == Part::kHeader) beginDrag(kMoveWindow, pt);
        if (h.part == Part::kVolume) beginDrag(kVolumeDrag, pt);
        if (h.part == Part::kCleaning) beginDrag(kCleaningDrag, pt);
        if (h.part == Part::kWave && !s.trim.raw.empty()) {
            int handle = handleAt(pt.x);
            beginDrag(handle ? handle : kNewSelection, pt);
        }
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const bool dragged = s.drag != kNoDrag;
        endDrag();
        Hit h = hitTest(pt), pressed = s.pressed;
        s.pressed = Hit();
        if (!dragged && h == pressed && enabledPart(h)) click(h);
        if (s.wnd && s.shown) setHover(hitTest(pt));
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == kDragTimer) {
            if (!buttonDown()) {
                endDrag();
                s.pressed = Hit();
            } else {
                POINT pt;
                GetCursorPos(&pt);
                dragTo(pt);
            }
        }
        return 0;
    case WM_DPICHANGED: {
        s.dpi = HIWORD(wp);
        makeFonts();  // la position reste celle choisie : seule la taille suit l'écran
        s.trim.peaksWidth = 0;
        rebuild();
        return 0;
    }
    case WM_CLOSE:  // Alt+F4 n'arrive jamais ici (pas de clavier), mais au cas où
        hide();
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------- confirmation en haut de l'écran

LRESULT CALLBACK toastProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;  // les clics passent au travers, jusqu'au jeu
    case WM_DPICHANGED: return 0;             // taille déjà calculée pour l'écran visé
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT r;
        GetClientRect(wnd, &r);
        HBRUSH bg = CreateSolidBrush(RGB(24, 25, 28));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        theme::circle(dc, float(MulDiv(16, int(s.toastDpi), 96)), float((r.bottom - r.top) / 2), float(MulDiv(4, int(s.toastDpi), 96)),
                      s.toastError ? theme::kDanger : theme::kAccentBright);
        RECT text = r;
        text.left += MulDiv(28, int(s.toastDpi), 96);
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, s.toastFont);
        SetTextColor(dc, s.toastError ? RGB(255, 170, 170) : theme::kText);
        DrawTextW(dc, s.toastText.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(wnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void roundCorners(HWND wnd) {
    DWORD round = 2;  // DWMWCP_ROUND (Windows 11)
    DwmSetWindowAttribute(wnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
}

bool ensureWindows() {
    if (s.wnd) return true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = overlayProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = toastProc;
    wc.lpszClassName = kToastClass;
    RegisterClassExW(&wc);

    // Toujours au-dessus, sans prendre la main, hors de la barre des tâches et d'Alt+Tab. Sans propriétaire :
    // il reste affiché quand la fenêtre de SoundZ est réduite ou dans la zone de notification.
    s.wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, kClass, L"SoundZ (overlay)", WS_POPUP,
                            0, 0, 10, 10, nullptr, nullptr, instance, nullptr);
    if (!s.wnd) return false;
    const int opacity = std::clamp(iniReadInt(kSection, L"opacite", 94), 40, 100);
    SetLayeredWindowAttributes(s.wnd, 0, BYTE(opacity * 255 / 100), LWA_ALPHA);
    roundCorners(s.wnd);

    s.toast = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT, kToastClass,
                              L"SoundZ", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, instance, nullptr);
    if (s.toast) {
        SetLayeredWindowAttributes(s.toast, 0, 235, LWA_ALPHA);
        roundCorners(s.toast);
    }
    return true;
}

HMONITOR gameMonitor() {
    HWND fg = GetForegroundWindow();
    return MonitorFromWindow(fg ? fg : ui.wnd, MONITOR_DEFAULTTOPRIMARY);
}

void showToast(const std::wstring& text, bool error) {
    if (!ensureWindows() || !s.toast) return;
    HMONITOR monitor = gameMonitor();
    UINT dpi = monitorDpi(monitor);
    if (dpi != s.toastDpi || !s.toastFont) {
        if (s.toastFont) DeleteObject(s.toastFont);
        s.toastFont = makeFont(dpi, 10, FW_SEMIBOLD);
        s.toastDpi = dpi;
    }
    s.toastText = text;
    s.toastError = error;
    HDC screen = GetDC(nullptr);
    HGDIOBJ old = SelectObject(screen, s.toastFont);
    RECT measure = {0, 0, 0, 0};
    DrawTextW(screen, text.c_str(), -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(screen, old);
    ReleaseDC(nullptr, screen);
    const int w = measure.right + MulDiv(28 + 16, int(dpi), 96), h = MulDiv(38, int(dpi), 96);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    const int x = (mi.rcMonitor.left + mi.rcMonitor.right - w) / 2, y = mi.rcMonitor.top + MulDiv(56, int(dpi), 96);
    SetWindowPos(s.toast, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(s.toast, nullptr, TRUE);
    s.toastTick = GetTickCount();
}

void show() {
    if (!ensureWindows()) return;
    // Là où l'utilisateur l'a laissé, sinon au centre de l'écran du jeu (en haut).
    HMONITOR monitor = gameMonitor();
    std::wstring sx = iniRead(kSection, L"x"), sy = iniRead(kSection, L"y");
    POINT saved = {_wtoi(sx.c_str()), _wtoi(sy.c_str())};
    bool hasSaved = !sx.empty() && !sy.empty();
    if (hasSaved) {
        HMONITOR m = MonitorFromPoint({saved.x + 20, saved.y + 20}, MONITOR_DEFAULTTONULL);
        if (m) monitor = m;
        else hasSaved = false;
    }
    s.dpi = monitorDpi(monitor);
    makeFonts();
    s.version = 0;
    s.voice = soundpage::soundsGoToMic();
    s.hear = soundpage::hearsInHeadset();
    s.playing = soundpage::playingMask();
    rebuild();
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    int x = saved.x, y = saved.y;
    if (!hasSaved) {
        x = (mi.rcWork.left + mi.rcWork.right - s.lay.width) / 2;
        y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top) / 6;
    }
    x = std::clamp(x, int(mi.rcWork.left), std::max(int(mi.rcWork.left), int(mi.rcWork.right) - s.lay.width));
    y = std::clamp(y, int(mi.rcWork.top), std::max(int(mi.rcWork.top), int(mi.rcWork.bottom) - s.lay.height));
    SetWindowPos(s.wnd, HWND_TOPMOST, x, y, s.lay.width, s.lay.height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    s.shown = true;
    s.topmostTick = GetTickCount();
    if (s.toast) ShowWindow(s.toast, SW_HIDE);  // l'overlay affiche lui-même les confirmations
    InvalidateRect(s.wnd, nullptr, FALSE);
}

}  // namespace

// ---------------------------------------------------------------- interface publique

void toggle() {
    if (s.shown) hide();
    else show();
}

void hide() {
    if (!s.wnd) return;
    endDrag();
    s.pressed = s.hover = Hit();
    ShowWindow(s.wnd, SW_HIDE);
    s.shown = false;
}

bool visible() { return s.shown; }

void notify(const std::wstring& text, bool error) {
    if (s.shown) {
        s.notice = text;
        s.noticeError = error;
        s.noticeTick = GetTickCount();
        InvalidateRect(s.wnd, nullptr, FALSE);
        return;
    }
    if (GetForegroundWindow() == ui.wnd) return;  // SoundZ au premier plan : sa page affiche déjà le message
    showToast(text, error);
}

void onTimer() {
    const DWORD now = GetTickCount();
    if (s.toast && IsWindowVisible(s.toast) && now - s.toastTick > kToastMs) ShowWindow(s.toast, SW_HIDE);
    if (!s.shown) return;
    if (s.pressed.part != Part::kNone && s.drag == kNoDrag && !buttonDown()) {  // bouton lâché hors de l'overlay
        s.pressed = Hit();
        InvalidateRect(s.wnd, nullptr, FALSE);
    }
    if (s.drag == kNoDrag && s.version != soundpage::boardVersion()) rebuild();
    const uint64_t playing = soundpage::playingMask();
    const bool voice = soundpage::soundsGoToMic(), hear = soundpage::hearsInHeadset();
    if (playing != s.playing || voice != s.voice || hear != s.hear) {
        s.playing = playing;
        s.voice = voice;
        s.hear = hear;
        InvalidateRect(s.wnd, nullptr, FALSE);
    }
    if (!s.notice.empty() && now - s.noticeTick > kNoticeMs) {
        s.notice.clear();
        InvalidateRect(s.wnd, nullptr, FALSE);
    }
    // Un autre programme s'est mis « toujours au-dessus » : on repasse devant, sans prendre la main.
    if (now - s.topmostTick > 2000) {
        s.topmostTick = now;
        SetWindowPos(s.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void shutdown() {
    if (s.wnd) DestroyWindow(s.wnd);
    if (s.toast) DestroyWindow(s.toast);
    s.wnd = s.toast = nullptr;
    s.shown = false;
    freeBitmaps();
    for (HFONT* f : {&s.font, &s.bold, &s.small, &s.tiny, &s.toastFont})
        if (*f) DeleteObject(*f);
    s.font = s.bold = s.small = s.tiny = s.toastFont = nullptr;
}

}  // namespace overlay
