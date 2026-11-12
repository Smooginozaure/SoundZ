#include "sound_image.h"

#include <d2d1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

namespace soundimg {
namespace {

IWICImagingFactory* g_wic = nullptr;
ID2D1Factory* g_d2d = nullptr;
IDWriteFactory* g_dwrite = nullptr;
// « Bicubique haute qualité » (Windows 10) ; absent des en-têtes de mingw.
constexpr WICBitmapInterpolationMode kHighQualityCubic = WICBitmapInterpolationMode(4);

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Copie une source WIC (déjà en BGRA prémultiplié) dans un carré transparent, centrée.
bool toImage(IWICBitmapSource* source, Image& out) {
    UINT w = 0, h = 0;
    if (FAILED(source->GetSize(&w, &h)) || w == 0 || h == 0) return false;
    double scale = std::min(double(kSize) / w, double(kSize) / h);
    UINT sw = std::max(1u, UINT(w * scale + 0.5)), sh = std::max(1u, UINT(h * scale + 0.5));
    IWICBitmapScaler* scaler = nullptr;
    if (FAILED(g_wic->CreateBitmapScaler(&scaler))) return false;
    bool ok = SUCCEEDED(scaler->Initialize(source, sw, sh, kHighQualityCubic));
    std::vector<uint32_t> part(size_t(sw) * sh);
    ok = ok && SUCCEEDED(scaler->CopyPixels(nullptr, sw * 4, UINT(part.size() * 4), reinterpret_cast<BYTE*>(part.data())));
    release(scaler);
    if (!ok) return false;
    out.pixels.assign(size_t(kSize) * kSize, 0);
    UINT x0 = (kSize - sw) / 2, y0 = (kSize - sh) / 2;
    for (UINT y = 0; y < sh; ++y)
        std::copy(part.begin() + long(y * sw), part.begin() + long((y + 1) * sw), out.pixels.begin() + long((y0 + y) * kSize + x0));
    return true;
}

}  // namespace

bool init() {
    if (g_wic) return true;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) return false;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dwrite));
    return true;
}

void shutdown() {
    release(g_dwrite);
    release(g_d2d);
    release(g_wic);
}

bool loadFile(const std::wstring& path, Image& out) {
    if (!g_wic) return false;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand, &decoder)) &&
              SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&converter)) &&
              SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                              0, WICBitmapPaletteTypeCustom)) &&
              toImage(converter, out);
    release(converter);
    release(frame);
    release(decoder);
    return ok;
}

bool renderText(const std::wstring& text, Image& out) {
    if (!g_wic || !g_d2d || !g_dwrite || text.empty()) return false;
    IWICBitmap* bitmap = nullptr;
    ID2D1RenderTarget* target = nullptr;
    IDWriteTextFormat* format = nullptr;
    IDWriteTextLayout* layout = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    bool ok = false;

    if (SUCCEEDED(g_wic->CreateBitmap(kSize, kSize, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap))) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96, 96);
        constexpr float kBox = 4000;  // grande boîte pour mesurer le dessin, puis mise à l'échelle
        if (SUCCEEDED(g_d2d->CreateWicBitmapRenderTarget(bitmap, props, &target)) &&
            SUCCEEDED(g_dwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 100, L"fr-fr",
                                                 &format)) &&
            SUCCEEDED(format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER)) &&
            SUCCEEDED(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) &&
            SUCCEEDED(g_dwrite->CreateTextLayout(text.c_str(), UINT32(text.size()), format, kBox, kBox, &layout)) &&
            SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.18f), &brush))) {
            // Contour réel du dessin (l'« encre »), pour qu'il remplisse le carré.
            DWRITE_OVERHANG_METRICS over = {};
            layout->GetOverhangMetrics(&over);
            float left = -over.left, top = -over.top;
            float width = kBox + over.left + over.right, height = kBox + over.top + over.bottom;
            if (width > 1 && height > 1) {
                float scale = kSize * 0.9f / std::max(width, height);
                float tx = kSize / 2.0f - scale * (left + width / 2), ty = kSize / 2.0f - scale * (top + height / 2);
                target->BeginDraw();
                target->Clear(D2D1::ColorF(0, 0, 0, 0));
                target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(tx, ty));
                target->DrawTextLayout(D2D1::Point2F(0, 0), layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                ok = SUCCEEDED(target->EndDraw());
            }
        }
    }
    if (ok) {
        out.pixels.assign(size_t(kSize) * kSize, 0);
        ok = SUCCEEDED(bitmap->CopyPixels(nullptr, kSize * 4, kSize * kSize * 4, reinterpret_cast<BYTE*>(out.pixels.data())));
    }
    release(brush);
    release(layout);
    release(format);
    release(target);
    release(bitmap);
    return ok;
}

// Dessin SVG (Direct2D sait les afficher depuis Windows 10 1703 : formes, dégradés, transformations ;
// ni script ni fichier extérieur, donc sans risque).
bool renderSvg(const std::string& svg, Image& out) {
    if (!g_wic || !g_d2d || svg.empty()) return false;
    IWICBitmap* bitmap = nullptr;
    ID2D1RenderTarget* target = nullptr;
    ID2D1DeviceContext5* context = nullptr;
    IStream* stream = nullptr;
    ID2D1SvgDocument* document = nullptr;
    bool ok = false;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (SUCCEEDED(g_wic->CreateBitmap(kSize, kSize, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) &&
        SUCCEEDED(g_d2d->CreateWicBitmapRenderTarget(bitmap, props, &target)) &&
        SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&context))) &&
        (stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(svg.data()), UINT(svg.size()))) != nullptr &&
        SUCCEEDED(context->CreateSvgDocument(stream, D2D1::SizeF(kSize, kSize), &document))) {
        context->BeginDraw();
        context->Clear(D2D1::ColorF(0, 0, 0, 0));
        context->DrawSvgDocument(document);
        ok = SUCCEEDED(context->EndDraw());
    }
    if (ok) {
        out.pixels.assign(size_t(kSize) * kSize, 0);
        ok = SUCCEEDED(bitmap->CopyPixels(nullptr, kSize * 4, kSize * kSize * 4, reinterpret_cast<BYTE*>(out.pixels.data())));
        // Un dessin entièrement transparent ne sert à rien.
        bool visible = false;
        for (uint32_t px : out.pixels) visible = visible || (px >> 24) > 16;
        ok = ok && visible;
    }
    release(document);
    release(stream);
    release(context);
    release(target);
    release(bitmap);
    return ok;
}

namespace {
// PNG écrit dans un flux (fichier ou mémoire). Pixels BGRA, prémultipliés ou non.
bool writePng(IStream* target, const uint32_t* pixels, int width, int height, bool premultiplied) {
    // PNG : couleurs « droites » (non prémultipliées).
    std::vector<uint32_t> straight(pixels, pixels + size_t(width) * size_t(height));
    if (premultiplied) {
        for (uint32_t& p : straight) {
            uint32_t a = p >> 24;
            if (a == 0 || a == 255) continue;
            uint32_t b = std::min(255u, (p & 0xFF) * 255 / a), g = std::min(255u, ((p >> 8) & 0xFF) * 255 / a),
                     r = std::min(255u, ((p >> 16) & 0xFF) * 255 / a);
            p = a << 24 | r << 16 | g << 8 | b;
        }
    }
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    const UINT w = UINT(width), h = UINT(height);
    bool ok = SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
              SUCCEEDED(encoder->Initialize(target, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
              IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
              SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, reinterpret_cast<BYTE*>(straight.data()))) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    release(frame);
    release(encoder);
    return ok;
}
}  // namespace

bool savePng(const std::wstring& path, const Image& image) {
    if (!g_wic || image.empty()) return false;
    IWICStream* stream = nullptr;
    bool ok = SUCCEEDED(g_wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              writePng(stream, image.pixels.data(), kSize, kSize, true);
    release(stream);
    return ok;
}

bool encodePng(const uint32_t* pixels, int width, int height, bool premultiplied, std::string& out) {
    out.clear();
    if (!g_wic || !pixels || width <= 0 || height <= 0) return false;
    IStream* stream = SHCreateMemStream(nullptr, 0);
    if (!stream) return false;
    bool ok = writePng(stream, pixels, width, height, premultiplied);
    STATSTG stat = {};
    ok = ok && SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME));
    if (ok) {
        out.resize(size_t(stat.cbSize.QuadPart));
        LARGE_INTEGER zero = {};
        ULONG read = 0;
        ok = SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)) &&
             SUCCEEDED(stream->Read(out.data(), ULONG(out.size()), &read)) && read == out.size();
    }
    stream->Release();
    if (!ok) out.clear();
    return ok;
}

HBITMAP makeBitmap(const Image& image, int sizePx) {
    if (!g_wic || image.empty() || sizePx <= 0) return nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = sizePx;
    info.bmiHeader.biHeight = -sizePx;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP result = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!result) return nullptr;

    IWICBitmap* source = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    bool ok = SUCCEEDED(g_wic->CreateBitmapFromMemory(kSize, kSize, GUID_WICPixelFormat32bppPBGRA, kSize * 4,
                                                      kSize * kSize * 4,
                                                      reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image.pixels.data())),
                                                      &source)) &&
              SUCCEEDED(g_wic->CreateBitmapScaler(&scaler)) &&
              SUCCEEDED(scaler->Initialize(source, UINT(sizePx), UINT(sizePx), sizePx < kSize ? WICBitmapInterpolationModeFant
                                                                                               : kHighQualityCubic)) &&
              SUCCEEDED(scaler->CopyPixels(nullptr, UINT(sizePx * 4), UINT(sizePx * sizePx * 4), static_cast<BYTE*>(bits)));
    release(scaler);
    release(source);
    if (!ok) {
        DeleteObject(result);
        return nullptr;
    }
    return result;
}

void draw(HDC dc, HBITMAP bitmap, int x, int y, int sizePx) {
    if (!bitmap) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bitmap);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, sizePx, sizePx, mem, 0, 0, sizePx, sizePx, blend);
    SelectObject(mem, old);
    DeleteDC(mem);
}

}  // namespace soundimg
