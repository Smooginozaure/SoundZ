#include "theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <map>
#include <vector>

// GDI+ (fourni par Windows) pour des arrondis lisses.
#include <objidl.h>
#include <gdiplus.h>

#include "ui.h"

namespace theme {
namespace {

ULONG_PTR g_gdiplus = 0;
std::map<COLORREF, HBRUSH> g_brushes;

// Le mode sombre des contrôles Windows passe par des fonctions de uxtheme.dll sans nom (seulement un numéro).
using AllowDarkFn = BOOL(WINAPI*)(HWND, BOOL);
using AppModeFn = int(WINAPI*)(int);
using FlushFn = void(WINAPI*)();
AllowDarkFn g_allowDark = nullptr;

const wchar_t kKindProp[] = L"SZ.kind";
const wchar_t kCheckProp[] = L"SZ.check";
const wchar_t kHoverProp[] = L"SZ.hover";
const wchar_t kBipolarProp[] = L"SZ.bipolar";
const wchar_t kSliderProp[] = L"SZ.slider";

struct InputFrame {
    HWND control;
    RECT rect;  // pixels, dans la fenêtre
};
std::vector<InputFrame> g_frames;

Gdiplus::Color gdi(COLORREF c, BYTE alpha = 255) { return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)); }

void roundPath(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    r = std::min({r, w / 2, h / 2});
    if (r <= 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    const float d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

Kind kindOf(HWND h) { return Kind(INT_PTR(GetPropW(h, kKindProp))); }

// Petit tampon de dessin : on dessine hors écran, puis on recopie d'un coup (pas de scintillement).
struct Canvas {
    HDC target, dc;
    HBITMAP bitmap;
    HGDIOBJ old;
    RECT where;
    int w, h;
    Canvas(HDC targetDc, const RECT& r) : target(targetDc), where(r), w(r.right - r.left), h(r.bottom - r.top) {
        dc = CreateCompatibleDC(target);
        bitmap = CreateCompatibleBitmap(target, std::max(1, w), std::max(1, h));
        old = SelectObject(dc, bitmap);
        RECT all = {0, 0, w, h};
        FillRect(dc, &all, brush(kBg));
        SetBkMode(dc, TRANSPARENT);
    }
    ~Canvas() {
        BitBlt(target, where.left, where.top, w, h, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};

std::wstring windowText(HWND h) {
    wchar_t buffer[256] = {};
    GetWindowTextW(h, buffer, 256);
    return buffer;
}

void drawText(HDC dc, const std::wstring& text, RECT r, COLORREF color, HFONT font, UINT format) {
    HGDIOBJ old = SelectObject(dc, font ? font : ui.font);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &r, format | DT_NOPREFIX);
    SelectObject(dc, old);
}

// ---- boutons : survol, état coché, double-clic traité comme un clic

LRESULT CALLBACK buttonProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, kHoverProp)) {
            SetPropW(h, kHoverProp, HANDLE(1));
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(h, kHoverProp);
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_LBUTTONDBLCLK:  // clics rapides (soundboard) : chaque clic compte
        return DefSubclassProc(h, WM_LBUTTONDOWN, wp, lp);
    case BM_SETCHECK:
        if (wp == BST_CHECKED) SetPropW(h, kCheckProp, HANDLE(1));
        else RemovePropW(h, kCheckProp);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case BM_GETCHECK:
        return GetPropW(h, kCheckProp) ? BST_CHECKED : BST_UNCHECKED;
    case WM_ENABLE:
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemovePropW(h, kKindProp);
        RemovePropW(h, kCheckProp);
        RemovePropW(h, kHoverProp);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void drawButton(const DRAWITEMSTRUCT* d, Kind kind) {
    Canvas c(d->hDC, d->rcItem);
    const HWND h = d->hwndItem;
    const bool pressed = d->itemState & ODS_SELECTED, disabled = d->itemState & ODS_DISABLED;
    const bool focus = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);
    const bool hover = isHover(h) && !disabled, checked = isChecked(h);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0));
    const std::wstring text = windowText(h);
    RECT box = {0, 0, c.w, c.h};
    const float radius = float(S(6));
    COLORREF fill = kButton, color = kText;

    switch (kind) {
    case Kind::kToggle: {
        // Interrupteur à gauche, libellé à droite.
        const int pw = S(30), ph = S(17);
        RECT pill = {0, (c.h - ph) / 2, pw, (c.h - ph) / 2 + ph};
        COLORREF track = checked ? (hover ? kAccentLight : kAccentBright) : (hover ? kFaint : kBorder);
        if (disabled) track = checked ? kAccentSoft : kSurface;
        roundRect(c.dc, pill, ph / 2.0f, track);
        const float knob = ph / 2.0f - S(3);
        const float kx = checked ? pill.right - ph / 2.0f : pill.left + ph / 2.0f;
        circle(c.dc, kx, (pill.top + pill.bottom) / 2.0f, knob, disabled ? kDisabled : checked ? RGB(255, 255, 255) : kMuted);
        if (focus) roundFrame(c.dc, RECT{pill.left, pill.top, pill.right, pill.bottom}, ph / 2.0f, kAccentLight, 1.5f);
        RECT label = {pw + S(9), 0, c.w, c.h};
        drawText(c.dc, text, label, disabled ? kDisabled : kText2, font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    case Kind::kPrimary:
        fill = pressed ? kAccent : hover ? kAccentBright : kAccentHover;
        color = RGB(255, 255, 255);
        if (disabled) fill = kSurface, color = kDisabled;
        break;
    case Kind::kSegment:
        fill = checked ? (hover ? kAccentHover : kAccent) : pressed ? kSurfacePressed : hover ? kButtonHover : kButton;
        color = checked ? RGB(255, 255, 255) : kText;
        if (disabled) fill = kSurface, color = kDisabled;
        break;
    case Kind::kTab:
        fill = checked ? kAccent : hover ? kSurface : kBg;
        color = checked ? RGB(255, 255, 255) : hover ? kText : kMuted;
        break;
    default:
        fill = pressed ? kSurfacePressed : hover ? kButtonHover : kButton;
        if (disabled) fill = kSurface, color = kDisabled;
        break;
    }
    if (fill != kBg) roundRect(c.dc, box, radius, fill);
    if (focus) roundFrame(c.dc, box, radius, kAccentLight, 1.5f);
    if (pressed) OffsetRect(&box, 0, 1);
    drawText(c.dc, text, box, color, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ---- curseurs

void drawSliderChannel(HDC dc, HWND h, const RECT& channel) {
    RECT thumb = {};
    SendMessageW(h, TBM_GETTHUMBRECT, 0, LPARAM(&thumb));
    const int cy = (channel.top + channel.bottom) / 2, th = S(4);
    const int thumbW = thumb.right - thumb.left;
    const int left = channel.left + thumbW / 2, right = channel.right - thumbW / 2;
    RECT track = {left, cy - th / 2, right, cy - th / 2 + th};
    roundRect(dc, track, th / 2.0f, kBorder);
    // Partie remplie : depuis la gauche, ou depuis le 0 central pour un curseur « ±».
    const int knob = (thumb.left + thumb.right) / 2;
    int from = left;
    if (GetPropW(h, kBipolarProp)) {
        int lo = int(SendMessageW(h, TBM_GETRANGEMIN, 0, 0)), hi = int(SendMessageW(h, TBM_GETRANGEMAX, 0, 0));
        from = hi > lo ? left + int(double(0 - lo) / double(hi - lo) * (right - left)) : left;
    }
    RECT filled = {std::min(from, knob), track.top, std::max(from, knob), track.bottom};
    if (filled.right > filled.left) roundRect(dc, filled, th / 2.0f, IsWindowEnabled(h) ? kAccentBright : kDisabled);
}

void drawSliderTics(HDC dc, HWND h) {
    if (GetWindowLongW(h, GWL_STYLE) & TBS_NOTICKS) return;  // crans seulement sur les curseurs « à pas »
    RECT channel = {}, thumb = {};
    SendMessageW(h, TBM_GETCHANNELRECT, 0, LPARAM(&channel));
    SendMessageW(h, TBM_GETTHUMBRECT, 0, LPARAM(&thumb));
    const int lo = int(SendMessageW(h, TBM_GETRANGEMIN, 0, 0)), hi = int(SendMessageW(h, TBM_GETRANGEMAX, 0, 0));
    const int thumbW = thumb.right - thumb.left;
    const int left = channel.left + thumbW / 2, right = channel.right - thumbW / 2;
    const float y = float(thumb.bottom) + S(3);
    for (int v = lo; v <= hi && hi > lo; ++v) {
        float x = left + float(v - lo) / float(hi - lo) * float(right - left);
        circle(dc, x, y, S(2) / 2.0f + 0.5f, kFaint);
    }
}

void drawSliderThumb(HDC dc, HWND h, const RECT& thumb) {
    RECT channel = {};
    SendMessageW(h, TBM_GETCHANNELRECT, 0, LPARAM(&channel));
    const float cx = (thumb.left + thumb.right) / 2.0f, cy = (channel.top + channel.bottom) / 2.0f;
    const bool hot = GetFocus() == h;
    circle(dc, cx, cy, S(7) + (hot ? 1.0f : 0.0f), IsWindowEnabled(h) ? RGB(255, 255, 255) : kDisabled);
}

// Le curseur est entièrement redessiné par nous, d'un seul coup et hors écran. Windows ne repeindrait que
// la zone autour du bouton rond : il resterait des morceaux de l'ancien bouton et de l'ancien remplissage.
LRESULT CALLBACK sliderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT client, channel = {}, thumb = {};
        GetClientRect(h, &client);
        SendMessageW(h, TBM_GETCHANNELRECT, 0, LPARAM(&channel));
        SendMessageW(h, TBM_GETTHUMBRECT, 0, LPARAM(&thumb));
        {
            Canvas c(dc, client);
            drawSliderChannel(c.dc, h, channel);
            drawSliderTics(c.dc, h);
            drawSliderThumb(c.dc, h, thumb);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemovePropW(h, kSliderProp);
        RemovePropW(h, kBipolarProp);
        break;
    }
    LRESULT result = DefSubclassProc(h, msg, wp, lp);
    switch (msg) {
    case TBM_SETPOS:
    case TBM_SETRANGE:
    case TBM_SETRANGEMIN:
    case TBM_SETRANGEMAX:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_MOUSEWHEEL:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_MOUSEMOVE:
        if (GetCapture() == h) InvalidateRect(h, nullptr, FALSE);  // pendant qu'on le fait glisser
        break;
    }
    return result;
}

// ---- case de raccourci

struct HotkeyState {
    WORD hotkey = 0;
    WORD invalid = 0;      // combinaisons refusées (HKCOMB_*)
    WORD fallback = 0;     // modificateurs mis à la place (HOTKEYF_*)
    HFONT font = nullptr;
    bool focus = false;
};

BYTE currentModifiers() {
    BYTE m = 0;
    if (GetKeyState(VK_SHIFT) < 0) m |= HOTKEYF_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0) m |= HOTKEYF_CONTROL;
    if (GetKeyState(VK_MENU) < 0) m |= HOTKEYF_ALT;
    return m;
}

bool refused(WORD invalid, BYTE mods) {
    const bool s = mods & HOTKEYF_SHIFT, c = mods & HOTKEYF_CONTROL, a = mods & HOTKEYF_ALT;
    WORD combo = !s && !c && !a ? HKCOMB_NONE
                 : s && !c && !a ? HKCOMB_S
                 : !s && c && !a ? HKCOMB_C
                 : !s && !c && a ? HKCOMB_A
                 : s && c && !a  ? HKCOMB_SC
                 : s && !c && a  ? HKCOMB_SA
                 : !s && c && a  ? HKCOMB_CA
                                 : HKCOMB_SCA;
    return invalid & combo;
}

void notifyChange(HWND h) {
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), EN_CHANGE), LPARAM(h));
}

LRESULT CALLBACK hotkeyProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<HotkeyState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(new HotkeyState()));
        break;
    case WM_NCDESTROY:
        delete s;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case HKM_SETHOTKEY:
        s->hotkey = WORD(wp);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case HKM_GETHOTKEY:
        return s->hotkey;
    case HKM_SETRULES:
        s->invalid = WORD(wp);
        s->fallback = LOWORD(lp);
        return 0;
    case WM_SETFONT:
        s->font = reinterpret_cast<HFONT>(wp);
        if (lp) InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_GETFONT:
        return LRESULT(s->font);
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        s->focus = msg == WM_SETFOCUS;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT vk = UINT(wp);
        BYTE mods = currentModifiers();
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
            InvalidateRect(h, nullptr, FALSE);  // affiche « Ctrl+Maj+… » pendant qu'on compose
            return 0;
        }
        if ((vk == VK_BACK || vk == VK_DELETE || vk == VK_ESCAPE) && mods == 0) {
            s->hotkey = 0;
        } else {
            if (refused(s->invalid, mods)) mods = BYTE(s->fallback);
            BYTE extra = (lp & (1 << 24)) ? HOTKEYF_EXT : 0;
            s->hotkey = MAKEWORD(BYTE(vk), BYTE(mods | extra));
        }
        InvalidateRect(h, nullptr, FALSE);
        notifyChange(h);
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        {
            Canvas c(dc, r);
            RECT box = {0, 0, c.w, c.h};
            roundRect(c.dc, box, float(S(6)), kInput);
            if (s->focus) roundFrame(c.dc, box, float(S(6)), kAccentLight, 1.5f);
            std::wstring text;
            COLORREF color = kText;
            BYTE mods = s->focus ? currentModifiers() : 0;
            if (s->focus && mods) {
                text = hotkeyText(MAKEWORD(0, mods));
                color = kMuted;
            } else if (LOBYTE(s->hotkey)) {
                text = hotkeyText(s->hotkey);
            } else {
                text = s->focus ? L"Tape une combinaison…" : L"Aucun";
                color = kFaint;
            }
            RECT label = {S(10), 0, c.w - S(8), c.h};
            drawText(c.dc, text, label, IsWindowEnabled(h) ? color : kDisabled, s->font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- jauge

struct MeterState {
    int lo = 0, hi = 100, pos = 0;
};

LRESULT CALLBACK meterProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<MeterState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(new MeterState()));
        break;
    case WM_NCDESTROY:
        delete s;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case PBM_SETRANGE32:
        s->lo = int(wp);
        s->hi = int(lp);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case PBM_SETPOS: {
        int old = s->pos;
        if (int(wp) != s->pos) {
            s->pos = int(wp);
            InvalidateRect(h, nullptr, FALSE);
        }
        return old;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        {
            Canvas c(dc, r);
            RECT box = {0, 0, c.w, c.h};
            roundRect(c.dc, box, c.h / 2.0f, kSurfacePressed);
            double t = s->hi > s->lo ? std::clamp(double(s->pos - s->lo) / double(s->hi - s->lo), 0.0, 1.0) : 0;
            RECT fill = {0, 0, int(t * c.w), c.h};
            if (fill.right >= c.h) roundRect(c.dc, fill, c.h / 2.0f, kAccentBright);
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------- initialisation

void init() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplus, &input, nullptr);

    if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        auto setMode = reinterpret_cast<AppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
        auto flush = reinterpret_cast<FlushFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
        g_allowDark = reinterpret_cast<AllowDarkFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
        if (setMode) setMode(2);  // ForceDark : menus et listes déroulantes sombres
        if (flush) flush();
    }

    WNDCLASSW hotkey = {};
    hotkey.lpfnWndProc = hotkeyProc;
    hotkey.hInstance = GetModuleHandleW(nullptr);
    hotkey.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    hotkey.lpszClassName = L"SZHotkey";
    RegisterClassW(&hotkey);
    WNDCLASSW meterClass = {};
    meterClass.lpfnWndProc = meterProc;
    meterClass.hInstance = GetModuleHandleW(nullptr);
    meterClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    meterClass.lpszClassName = L"SZMeter";
    RegisterClassW(&meterClass);
}

void shutdown() {
    for (auto& b : g_brushes) DeleteObject(b.second);
    g_brushes.clear();
    if (g_gdiplus) Gdiplus::GdiplusShutdown(g_gdiplus);
    g_gdiplus = 0;
}

void decorateWindow(HWND top) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(top, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = kAccent, text = RGB(255, 255, 255), border = kAccent;
    DwmSetWindowAttribute(top, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(top, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
    DwmSetWindowAttribute(top, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    if (g_allowDark) g_allowDark(top, TRUE);
    // Pas de pointillés de focus à la souris (ils restent au clavier).
    SendMessageW(top, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

HBRUSH brush(COLORREF color) {
    auto it = g_brushes.find(color);
    if (it != g_brushes.end()) return it->second;
    HBRUSH b = CreateSolidBrush(color);
    g_brushes[color] = b;
    return b;
}

// ---------------------------------------------------------------- contrôles

HWND button(const wchar_t* text, Kind kind, int id) {
    DWORD style = WS_TABSTOP | BS_OWNERDRAW;
    HWND h = makeControl(L"BUTTON", text, style, id);
    SetPropW(h, kKindProp, HANDLE(INT_PTR(kind)));
    SetWindowSubclass(h, buttonProc, 1, 0);
    return h;
}

HWND hotkeyBox(int id) { return makeControl(L"SZHotkey", L"", WS_TABSTOP, id); }

HWND meter(int id) { return makeControl(L"SZMeter", L"", 0, id); }

void slider(HWND trackbar, bool bipolar) {
    SetPropW(trackbar, kSliderProp, HANDLE(1));
    if (bipolar) SetPropW(trackbar, kBipolarProp, HANDLE(1));
    else RemovePropW(trackbar, kBipolarProp);
    SetWindowSubclass(trackbar, sliderProc, 2, 0);
    InvalidateRect(trackbar, nullptr, FALSE);
}

void darkControl(HWND control) {
    if (g_allowDark) g_allowDark(control, TRUE);
    SetWindowTheme(control, L"DarkMode_CFD", nullptr);
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
}

void placeInput(HWND edit, int x, int y, int width, int height, HFONT font) {
    RECT frame = {S(x), S(y), S(x + width), S(y + height)};
    HDC dc = GetDC(edit);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(edit, dc);
    const int line = tm.tmHeight + S(2);
    MoveWindow(edit, frame.left + S(10), frame.top + (frame.bottom - frame.top - line) / 2, frame.right - frame.left - S(20), line,
               TRUE);
    SendMessageW(edit, WM_SETFONT, WPARAM(font), TRUE);
    for (InputFrame& f : g_frames) {
        if (f.control == edit) {
            f.rect = frame;
            return;
        }
    }
    g_frames.push_back({edit, frame});
}

bool isHover(HWND control) { return GetPropW(control, kHoverProp) != nullptr; }
bool isChecked(HWND control) { return GetPropW(control, kCheckProp) != nullptr; }

// ---------------------------------------------------------------- messages de la fenêtre

void onCommand(HWND control, int code) {
    if (!control) return;
    if (code == BN_CLICKED && kindOf(control) == Kind::kToggle)
        SendMessageW(control, BM_SETCHECK, isChecked(control) ? BST_UNCHECKED : BST_CHECKED, 0);
    if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {  // le cadre du champ change de couleur
        for (const InputFrame& f : g_frames)
            if (f.control == control) InvalidateRect(GetParent(control), &f.rect, FALSE);
    }
}

bool drawItem(const DRAWITEMSTRUCT* item) {
    if (item->CtlType != ODT_BUTTON) return false;
    Kind kind = kindOf(item->hwndItem);
    if (kind == Kind::kNone || kind == Kind::kTile) return false;
    drawButton(item, kind);
    return true;
}

bool customDraw(const NMHDR* header, LRESULT& result) {
    if (header->code != NM_CUSTOMDRAW || !GetPropW(header->hwndFrom, kSliderProp)) return false;
    const auto* cd = reinterpret_cast<const NMCUSTOMDRAW*>(header);
    HWND h = header->hwndFrom;
    switch (cd->dwDrawStage) {
    case CDDS_PREPAINT:
        result = CDRF_NOTIFYITEMDRAW;
        return true;
    case CDDS_ITEMPREPAINT:
        if (cd->dwItemSpec == TBCD_CHANNEL) drawSliderChannel(cd->hdc, h, cd->rc);
        else if (cd->dwItemSpec == TBCD_THUMB) drawSliderThumb(cd->hdc, h, cd->rc);
        else if (cd->dwItemSpec == TBCD_TICS) drawSliderTics(cd->hdc, h);
        result = CDRF_SKIPDEFAULT;
        return true;
    }
    result = CDRF_DODEFAULT;
    return true;
}

LRESULT ctlColor(UINT message, HDC dc, HWND control, COLORREF text) {
    SetTextColor(dc, text);
    if (message == WM_CTLCOLOREDIT) {
        // Champ d'une liste déroulante modifiable : couleur de la liste ; sinon, couleur des champs.
        wchar_t cls[32] = {};
        GetClassNameW(GetParent(control), cls, 32);
        COLORREF back = lstrcmpiW(cls, L"ComboBox") == 0 ? kButton : kInput;
        SetBkColor(dc, back);
        return LRESULT(brush(back));
    }
    if (message == WM_CTLCOLORLISTBOX) {
        SetBkColor(dc, kSurface);
        return LRESULT(brush(kSurface));
    }
    SetBkMode(dc, TRANSPARENT);
    SetBkColor(dc, kBg);
    return LRESULT(brush(kBg));
}

void paint(HDC dc) {
    for (const InputFrame& f : g_frames) {
        if (!IsWindowVisible(f.control)) continue;
        roundRect(dc, f.rect, float(S(6)), kInput);
        if (GetFocus() == f.control) roundFrame(dc, f.rect, float(S(6)), kAccentLight, 1.5f);
    }
}

// ---------------------------------------------------------------- dessin

void roundRect(HDC dc, const RECT& r, float radius, COLORREF fill) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    roundPath(path, float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top), radius);
    Gdiplus::SolidBrush b(gdi(fill));
    g.FillPath(&b, &path);
}

void roundFrame(HDC dc, const RECT& r, float radius, COLORREF color, float width, bool dashed) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    const float inset = width / 2;
    roundPath(path, r.left + inset, r.top + inset, r.right - r.left - width, r.bottom - r.top - width, radius - inset);
    Gdiplus::Pen pen(gdi(color), width);
    if (dashed) pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawPath(&pen, &path);
}

void circle(HDC dc, float cx, float cy, float radius, COLORREF fill) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::SolidBrush b(gdi(fill));
    g.FillEllipse(&b, cx - radius, cy - radius, radius * 2, radius * 2);
}

// ---------------------------------------------------------------- raccourcis

namespace {
std::wstring keyName(UINT vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::wstring(1, wchar_t(vk));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Pavé " + std::to_wstring(vk - VK_NUMPAD0);
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    LONG lp = LONG(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE:
        lp |= 1 << 24;
    }
    wchar_t name[64] = {};
    if (GetKeyNameTextW(lp, name, 64) > 0) return name;
    return L"?";
}
}  // namespace

std::wstring hotkeyText(WORD hk) {
    BYTE mods = HIBYTE(hk);
    std::wstring s;
    if (mods & HOTKEYF_CONTROL) s += L"Ctrl+";
    if (mods & HOTKEYF_SHIFT) s += L"Maj+";
    if (mods & HOTKEYF_ALT) s += L"Alt+";
    if (!LOBYTE(hk)) return s.empty() ? L"" : s + L"…";
    return s + keyName(LOBYTE(hk));
}

}  // namespace theme
