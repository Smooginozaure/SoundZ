// Tampon « élastique » mono entre deux appareils audio qui n'ont pas exactement la même horloge
// (le micro et CABLE-A par exemple). Le lecteur lit un poil plus vite ou plus lentement
// (interpolation cubique, ±0,1 % au maximum) pour garder une petite avance constante.
// Un seul thread écrit, un seul thread lit.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

class ElasticBuffer {
public:
    explicit ElasticBuffer(int targetFrames = 720) : target_(targetFrames) { buffer_.assign(kSize, 0.0f); }

    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }

    void reset() {
        underruns_ = 0;
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        written_ = 0;
        read_ = 0;
        readBase_ = 1;
        frac_ = 0;
        flowing_ = false;
    }

    // Thread producteur.
    void push(const float* x, int n) {
        uint64_t w = written_.load(std::memory_order_relaxed);
        uint64_t r = read_.load(std::memory_order_acquire);
        if (w + uint64_t(n) - r > kSize - 8) return;  // lecteur arrêté : on jette
        for (int i = 0; i < n; ++i) buffer_[size_t((w + uint64_t(i)) & kMask)] = x[i];
        written_.store(w + uint64_t(n), std::memory_order_release);
    }

    // Thread consommateur : produit toujours exactement n échantillons (du silence si rien n'arrive).
    void pull(float* out, int n) {
        uint64_t w = written_.load(std::memory_order_acquire);
        double available = double(w) - double(readBase_);
        if (!flowing_ && available >= target_ + n + 3) {
            readBase_ = w - uint64_t(target_);  // on démarre pile avec l'avance voulue
            frac_ = 0;
            available = target_;
            flowing_ = true;
            fill_ = available;
        }
        if (!flowing_) {
            std::fill(out, out + n, 0.0f);
            return;
        }
        if (available > target_ * 6 + 4800) {  // gros retard (à-coup du système) : on saute au présent
            readBase_ = w - uint64_t(target_);
            frac_ = 0;
            available = target_;
            fill_ = available;
        }
        fill_ += (available - fill_) * 0.02;
        // Correction très douce (±0,1 % max) : les horloges ne diffèrent que de quelques millièmes de %,
        // et une correction plus forte s'entendrait comme une voix légèrement fausse.
        double ratio = 1.0 + std::clamp((fill_ - target_) / target_ * 0.001, -0.001, 0.001);
        for (int i = 0; i < n; ++i) {
            if (readBase_ + 2 >= w) {  // plus rien : silence et on attendra
                underruns_.fetch_add(1, std::memory_order_relaxed);
                flowing_ = false;
                std::fill(out + i, out + n, 0.0f);
                break;
            }
            float xm1 = at(readBase_ - 1), x0 = at(readBase_), x1 = at(readBase_ + 1), x2 = at(readBase_ + 2);
            float t = float(frac_);
            // Interpolation cubique (Catmull-Rom).
            float c1 = 0.5f * (x1 - xm1);
            float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            out[i] = ((c3 * t + c2) * t + c1) * t + x0;
            frac_ += ratio;
            while (frac_ >= 1.0) { frac_ -= 1.0; ++readBase_; }
        }
        read_.store(readBase_ > 2 ? readBase_ - 2 : 0, std::memory_order_release);
    }

private:
    static constexpr uint64_t kSize = 1 << 15;
    static constexpr uint64_t kMask = kSize - 1;
    float at(uint64_t i) const { return buffer_[size_t(i & kMask)]; }

    std::vector<float> buffer_;
    std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> read_{0};
    std::atomic<uint64_t> underruns_{0};
    // Côté lecteur uniquement.
    uint64_t readBase_ = 1;
    double frac_ = 0;
    double fill_ = 0;
    bool flowing_ = false;
    double target_;
};
