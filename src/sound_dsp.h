// Soundboard : nettoyage d'un son capturé (souffle, bruits de fond), découpe des silences,
// volume homogène ; lecture / écriture de fichiers WAV.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sound {

constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- FFT (radix 2, en place)

inline void fft(std::vector<std::complex<float>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = 2 * kPi / double(len) * (inverse ? 1 : -1);
        std::complex<float> wlen(float(std::cos(angle)), float(std::sin(angle)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= float(n);
}

// ---------------------------------------------------------------- étapes du nettoyage

// Coupe seulement l'infra-grave inaudible (décalage continu, vibrations) sous ~25 Hz : les basses restent.
inline void highPass(std::vector<float>& x, float cutoff = 25.0f) {
    const double w = 2 * kPi * cutoff / kRate, q = 0.7071;
    const double alpha = std::sin(w) / (2 * q), c = std::cos(w);
    const double a0 = 1 + alpha;
    const double b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = (1 + c) / 2 / a0;
    const double a1 = -2 * c / a0, a2 = (1 - alpha) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (float& s : x) {
        double y = b0 * s + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = s;
        y2 = y1;
        y1 = y;
        s = float(y);
    }
}

// Réduction du souffle (bruit blanc) : le « fond » de chaque fréquence est mesuré sur les moments les plus
// calmes du son, puis retiré. Précautions, pour ne jamais abîmer le son lui-même :
//  • les graves ne sont pas touchés (le souffle s'entend dans les aigus ; une basse n'est pas du bruit) ;
//  • si le son n'a pas de vraies pauses (de la musique continue), les « moments calmes » sont encore de la
//    musique : la réduction devient alors très légère ;
//  • au plus −18 dB, gains lissés dans le temps et en fréquence (pas d'effet « bulles »).
// strength : 0 (rien) à 1 (fort).
inline void denoise(std::vector<float>& x, float strength) {
    constexpr int N = 1024, hop = 256, bins = N / 2 + 1;
    if (x.size() < size_t(N) || strength <= 0) return;
    std::vector<float> window(N);
    for (int i = 0; i < N; ++i) window[size_t(i)] = float(std::sqrt(0.5 - 0.5 * std::cos(2 * kPi * i / N)));

    std::vector<float> padded(size_t(N) + x.size() + size_t(N), 0.0f);
    std::copy(x.begin(), x.end(), padded.begin() + N);
    const int frames = int((padded.size() - size_t(N)) / hop) + 1;

    std::vector<std::vector<std::complex<float>>> spectra(static_cast<size_t>(frames), std::vector<std::complex<float>>(N));
    std::vector<float> power(size_t(frames) * bins);
    for (int f = 0; f < frames; ++f) {
        auto& s = spectra[size_t(f)];
        for (int i = 0; i < N; ++i) {
            size_t at = size_t(f) * hop + size_t(i);
            s[size_t(i)] = at < padded.size() ? padded[at] * window[size_t(i)] : 0.0f;
        }
        fft(s, false);
        for (int k = 0; k < bins; ++k) power[size_t(f) * bins + size_t(k)] = std::norm(s[size_t(k)]);
    }

    // Fond de bruit : spectre moyen des 20 % de moments les plus calmes (hors bords vides).
    // On choisit les moments sur leur énergie totale, puis on fait la moyenne fréquence par fréquence :
    // une moyenne sur beaucoup de moments donne le vrai niveau du souffle, sans biais.
    const int first = N / hop, last = std::max(first + 1, frames - N / hop);
    std::vector<std::pair<double, int>> energy;
    for (int f = first; f < last; ++f) {
        double e = 0;
        for (int k = 0; k < bins; ++k) e += power[size_t(f) * bins + size_t(k)];
        energy.push_back({e, f});
    }
    std::sort(energy.begin(), energy.end());
    const size_t quiet = std::max<size_t>(1, energy.size() / 5);
    std::vector<float> noise(bins, 0.0f);
    for (size_t i = 0; i < quiet; ++i)
        for (int k = 0; k < bins; ++k) noise[size_t(k)] += power[size_t(energy[i].second) * bins + size_t(k)] / float(quiet);
    std::vector<float> smoothNoise(noise);
    for (int k = 1; k + 1 < bins; ++k)
        smoothNoise[size_t(k)] = (noise[size_t(k - 1)] + 2 * noise[size_t(k)] + noise[size_t(k + 1)]) / 4;

    // Y a-t-il un vrai fond de bruit ? Les moments forts doivent dépasser nettement les plus calmes.
    // 6 dB d'écart ou moins : rien que du son, sans pause (réduction quasi nulle) ; 20 dB et plus : vraies pauses.
    double quietEnergy = 0;
    for (size_t i = 0; i < quiet; ++i) quietEnergy += energy[i].first / double(quiet);
    const double loudEnergy = energy[energy.size() * 4 / 5].first;
    const double contrastDb = 10 * std::log10(std::max(loudEnergy, 1e-20) / std::max(quietEnergy, 1e-20));
    const float trust = float(std::clamp((contrastDb - 6) / 14, 0.1, 1.0));
    const float amount = std::clamp(strength, 0.0f, 1.0f) * trust;
    const float oversubtract = 1.0f + 1.5f * amount;

    // Plus faible gain permis à chaque fréquence : 1 (rien) sous 300 Hz, jusqu'à −18 dB au-dessus de 2 kHz.
    std::vector<float> floorGain(bins);
    for (int k = 0; k < bins; ++k) {
        double hz = double(k) * kRate / N;
        double t = hz <= 300 ? 0 : std::min(1.0, std::log2(hz / 300) / std::log2(2000.0 / 300));
        floorGain[size_t(k)] = float(std::pow(10.0, -18.0 * amount * t / 20));
    }

    std::vector<float> held(bins, 1.0f), smoothed(bins);
    std::vector<float> out(padded.size(), 0.0f);
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < bins; ++k) {
            float p = power[size_t(f) * bins + size_t(k)];
            float g = p > 0 ? 1.0f - oversubtract * smoothNoise[size_t(k)] / p : 0.0f;
            g = std::max(floorGain[size_t(k)], g);
            // Monte vite (attaques nettes), redescend doucement (pas de bulles).
            held[size_t(k)] = g > held[size_t(k)] ? g : held[size_t(k)] * 0.6f + g * 0.4f;
        }
        for (int k = 0; k < bins; ++k) {
            float a = held[size_t(std::max(0, k - 1))], b = held[size_t(k)], c = held[size_t(std::min(bins - 1, k + 1))];
            smoothed[size_t(k)] = (a + 2 * b + c) / 4;
        }
        auto& s = spectra[size_t(f)];
        for (int k = 0; k < bins; ++k) {
            s[size_t(k)] *= smoothed[size_t(k)];
            if (k > 0 && k < N / 2) s[size_t(N - k)] = std::conj(s[size_t(k)]);
        }
        fft(s, true);
        for (int i = 0; i < N; ++i) {
            size_t at = size_t(f) * hop + size_t(i);
            if (at < out.size()) out[at] += s[size_t(i)].real() * window[size_t(i)] / 2.0f;  // somme des fenêtres = 2
        }
    }
    std::copy(out.begin() + N, out.begin() + N + long(x.size()), x.begin());
}

// Énergie par tranches de 10 ms.
inline std::vector<float> rmsFrames(const std::vector<float>& x, int frame) {
    std::vector<float> r;
    for (size_t i = 0; i < x.size(); i += size_t(frame)) {
        size_t end = std::min(x.size(), i + size_t(frame));
        double sum = 0;
        for (size_t j = i; j < end; ++j) sum += double(x[j]) * x[j];
        r.push_back(float(std::sqrt(sum / double(end - i))));
    }
    return r;
}

struct CleanResult {
    std::vector<float> samples;
    bool empty = true;  // rien d'audible dans la capture
};

// Nettoyage complet : infra-grave, souffle (selon strength, 0 à 1), silences du début et de la fin,
// fondus très courts, puis volume homogène (crête à −1 dB au maximum).
inline CleanResult clean(std::vector<float> x, float strength) {
    CleanResult result;
    if (x.empty()) return result;
    highPass(x);
    denoise(x, strength);

    const int frame = kRate / 100;
    std::vector<float> rms = rmsFrames(x, frame);
    float peakRms = *std::max_element(rms.begin(), rms.end());
    if (peakRms < 1e-4f) return result;  // −80 dB : silence
    // Est « audible » ce qui dépasse nettement le fond (+10 dB) et n'est pas 30 dB sous le plus fort.
    std::vector<float> sorted(rms);
    std::sort(sorted.begin(), sorted.end());
    const float floorRms = sorted[sorted.size() / 5];
    const float threshold = std::min(std::max({peakRms * 0.03f, floorRms * 3.2f, 0.001f}), peakRms * 0.1f);
    int firstFrame = -1, lastFrame = -1;
    for (int i = 0; i < int(rms.size()); ++i) {
        if (rms[size_t(i)] >= threshold) {
            if (firstFrame < 0) firstFrame = i;
            lastFrame = i;
        }
    }
    size_t start = size_t(std::max(0, firstFrame - 2)) * size_t(frame);  // 20 ms avant l'attaque
    size_t end = std::min(x.size(), size_t(lastFrame + 16) * size_t(frame));  // 150 ms de queue
    std::vector<float> y(x.begin() + long(start), x.begin() + long(end));

    const size_t fadeIn = std::min(y.size() / 4, size_t(kRate * 3 / 1000));
    const size_t fadeOut = std::min(y.size() / 4, size_t(kRate * 40 / 1000));
    for (size_t i = 0; i < fadeIn; ++i) y[i] *= float(i) / float(fadeIn);
    for (size_t i = 0; i < fadeOut; ++i) y[y.size() - 1 - i] *= float(i) / float(fadeOut);

    // Volume : niveau moyen des passages audibles à −16 dB, sans que la crête dépasse −1 dB.
    double sum = 0;
    size_t count = 0;
    for (int i = firstFrame; i <= lastFrame; ++i) {
        if (rms[size_t(i)] >= threshold) {
            sum += double(rms[size_t(i)]) * rms[size_t(i)];
            ++count;
        }
    }
    float loud = float(std::sqrt(sum / double(std::max<size_t>(1, count))));
    float peak = 0;
    for (float s : y) peak = std::max(peak, std::fabs(s));
    float g = std::min(0.158f / std::max(loud, 1e-6f), 0.891f / std::max(peak, 1e-6f));
    g = std::min(g, 30.0f);  // +30 dB au plus
    for (float& s : y) s *= g;

    result.samples = std::move(y);
    result.empty = false;
    return result;
}

// ---------------------------------------------------------------- WAV (48 kHz, mono, 16 bits)

inline bool writeWav(const std::wstring& path, const std::vector<float>& x) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    uint32_t bytes = uint32_t(x.size() * 2);
    fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(1);
    u32(kRate);
    u32(kRate * 2);
    u16(2);
    u16(16);
    fwrite("data", 1, 4, f);
    u32(bytes);
    std::vector<int16_t> pcm(x.size());
    for (size_t i = 0; i < x.size(); ++i) pcm[i] = int16_t(std::lround(std::clamp(x[i], -1.0f, 1.0f) * 32767.0f));
    bool ok = fwrite(pcm.data(), 2, pcm.size(), f) == pcm.size();
    fclose(f);
    return ok;
}

// Lit un WAV PCM 16 bits ou flottant 32 bits ; mélange en mono. (Pas de rééchantillonnage : 48 kHz attendu.)
inline bool readWav(const std::wstring& path, std::vector<float>& out) {
    out.clear();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::vector<uint8_t> data;
    uint8_t buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) data.insert(data.end(), buffer, buffer + n);
    fclose(f);
    if (data.size() < 12 || std::string(data.begin(), data.begin() + 4) != "RIFF" ||
        std::string(data.begin() + 8, data.begin() + 12) != "WAVE")
        return false;
    auto rd16 = [&](size_t at) { return uint16_t(data[at] | data[at + 1] << 8); };
    auto rd32 = [&](size_t at) { return uint32_t(rd16(at) | uint32_t(rd16(at + 2)) << 16); };
    int format = 0, channels = 0, bits = 0;
    for (size_t at = 12; at + 8 <= data.size();) {
        std::string id(data.begin() + long(at), data.begin() + long(at) + 4);
        size_t size = rd32(at + 4), body = at + 8;
        if (body + size > data.size()) size = data.size() - body;
        if (id == "fmt " && size >= 16) {
            format = rd16(body);
            channels = rd16(body + 2);
            bits = rd16(body + 14);
            if (format == 0xFFFE && size >= 26) format = rd16(body + 24);  // WAVE_FORMAT_EXTENSIBLE
        } else if (id == "data" && channels > 0) {
            if (format == 1 && bits == 16) {
                size_t frames = size / 2 / size_t(channels);
                out.resize(frames);
                for (size_t i = 0; i < frames; ++i) {
                    float s = 0;
                    for (int c = 0; c < channels; ++c) s += float(int16_t(rd16(body + (i * size_t(channels) + size_t(c)) * 2)));
                    out[i] = s / 32768.0f / float(channels);
                }
                return true;
            }
            if (format == 3 && bits == 32) {
                size_t frames = size / 4 / size_t(channels);
                out.resize(frames);
                for (size_t i = 0; i < frames; ++i) {
                    float s = 0;
                    for (int c = 0; c < channels; ++c) {
                        uint32_t v = rd32(body + (i * size_t(channels) + size_t(c)) * 4);
                        float x;
                        memcpy(&x, &v, 4);
                        s += x;
                    }
                    out[i] = s / float(channels);
                }
                return true;
            }
            return false;
        }
        at = body + size + (size & 1);
    }
    return false;
}

}  // namespace sound
