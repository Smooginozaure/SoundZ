// Soundboard : images des boutons (emoji dessiné en couleur, ou image du PC), au format carré 128 × 128.
// S'appuie sur WIC et Direct2D / DirectWrite, fournis par Windows.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace soundimg {

constexpr int kSize = 128;

struct Image {
    std::vector<uint32_t> pixels;  // kSize × kSize, BGRA prémultiplié (comme AlphaBlend l'attend)
    bool empty() const { return pixels.empty(); }
};

bool init();  // après CoInitializeEx
void shutdown();

bool loadFile(const std::wstring& path, Image& out);  // PNG, JPG, GIF, BMP… : centré, sans déformer
bool renderText(const std::wstring& text, Image& out);  // emoji (en couleur) ou court texte
bool renderSvg(const std::string& svg, Image& out);     // dessin vectoriel SVG (texte UTF-8)
bool savePng(const std::wstring& path, const Image& image);
// PNG en mémoire (télécommande du téléphone). Pixels BGRA, prémultipliés ou non.
bool encodePng(const uint32_t* pixels, int width, int height, bool premultiplied, std::string& out);

// Image à la taille voulue (pixels), pour AlphaBlend. À libérer avec DeleteObject.
HBITMAP makeBitmap(const Image& image, int sizePx);
void draw(HDC dc, HBITMAP bitmap, int x, int y, int sizePx);

}  // namespace soundimg
