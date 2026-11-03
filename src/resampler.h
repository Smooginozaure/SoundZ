// Rééchantillonneur « sinc fenêtré » (32 points) à rapport variable, pour la transposition.
//
// Principe de la transposition : on étire d'abord le son dans le temps sans changer sa hauteur
// (signalsmith-stretch, très juste), puis on le relit plus vite ou plus lentement ici.
// La hauteur obtenue dépend alors uniquement du rapport de lecture : elle est exacte.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

template <int Channels>
class Resampler {
public:
    static constexpr int kHalf = 16;      // points de chaque côté
    static constexpr int kPhases = 256;   // résolution de la table (interpolée linéairement)

    // Prépare une table de filtres par rapport possible (les tables sont calculées une fois pour toutes).
    void prepare(const std::vector<double>& ratios, int maxOutput) {
        ratios_ = ratios;
        tables_.assign(ratios.size(), std::vector<float>());
        for (size_t i = 0; i < ratios.size(); ++i) buildTable(tables_[i], ratios[i]);
        capacity_ = size_t(std::ceil(maxOutput * maxRatio())) + 4 * kHalf + 8;
        for (auto& b : buffer_) b.assign(capacity_, 0.0f);
        reset();
    }

    void reset() {
        for (auto& b : buffer_) std::fill(b.begin(), b.end(), 0.0f);
        count_ = 2 * kHalf;
        position_ = double(kHalf);
    }

    void select(size_t ratioIndex) { current_ = ratioIndex; }
    double ratio() const { return ratios_[current_]; }

    // Nombre d'échantillons étirés à fournir pour produire `outputFrames` échantillons.
    int needed(int outputFrames) const {
        double last = position_ + (outputFrames - 1) * ratio();
        long long need = (long long)std::floor(last) + kHalf + 1 - (long long)count_;
        return int(std::max<long long>(0, need));
    }

    // Où écrire les `needed()` échantillons étirés (un pointeur par canal).
    float* writePointer(int channel) { return buffer_[size_t(channel)].data() + count_; }
    void commit(int frames) { count_ += size_t(frames); }

    void process(float* const* output, int outputFrames) {
        const double r = ratio();
        const std::vector<float>& table = tables_[current_];
        for (int i = 0; i < outputFrames; ++i) {
            double base = std::floor(position_);
            double frac = position_ - base;
            double phase = frac * kPhases;
            int p = int(phase);
            float t = float(phase - p);
            const float* h0 = &table[size_t(p) * (2 * kHalf)];
            const float* h1 = h0 + 2 * kHalf;
            size_t start = size_t(base) - kHalf + 1;
            for (int c = 0; c < Channels; ++c) {
                const float* x = buffer_[size_t(c)].data() + start;
                float acc0 = 0, acc1 = 0;
                for (int k = 0; k < 2 * kHalf; ++k) {
                    acc0 += x[k] * h0[k];
                    acc1 += x[k] * h1[k];
                }
                output[c][i] = acc0 + (acc1 - acc0) * t;
            }
            position_ += r;
        }
        // On jette ce qui ne sert plus.
        long long drop = (long long)std::floor(position_) - kHalf - 1;
        if (drop > 0) {
            for (auto& b : buffer_) std::copy(b.begin() + drop, b.begin() + (long long)count_, b.begin());
            count_ -= size_t(drop);
            position_ -= double(drop);
        }
    }

private:
    double maxRatio() const {
        double m = 1;
        for (double r : ratios_) m = std::max(m, r);
        return m;
    }

    static double besselI0(double x) {
        double sum = 1, term = 1;
        for (int k = 1; k < 30; ++k) {
            term *= (x / (2 * k)) * (x / (2 * k));
            sum += term;
        }
        return sum;
    }

    // table[p][k] = filtre pour une position fractionnaire p/kPhases (kPhases + 1 lignes).
    static void buildTable(std::vector<float>& table, double ratio) {
        const double pi = 3.14159265358979323846;
        const double beta = 8.0;
        // En lisant plus vite (ratio > 1), on coupe les aigus qui se replieraient.
        const double cutoff = std::min(1.0, 1.0 / ratio) * 0.97;
        table.assign(size_t(kPhases + 1) * (2 * kHalf), 0.0f);
        for (int p = 0; p <= kPhases; ++p) {
            double frac = double(p) / kPhases;
            double sum = 0;
            float* row = &table[size_t(p) * (2 * kHalf)];
            for (int k = 0; k < 2 * kHalf; ++k) {
                double t = double(k - kHalf + 1) - frac;  // distance au point lu
                double x = cutoff * t;
                double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(pi * x) / (pi * x);
                double w = std::fabs(t) >= kHalf ? 0.0 : besselI0(beta * std::sqrt(1 - (t / kHalf) * (t / kHalf))) / besselI0(beta);
                row[k] = float(cutoff * sinc * w);
                sum += row[k];
            }
            for (int k = 0; k < 2 * kHalf; ++k) row[k] = float(row[k] / sum);  // gain unitaire
        }
    }

    std::vector<double> ratios_;
    std::vector<std::vector<float>> tables_;
    std::vector<float> buffer_[Channels];
    size_t capacity_ = 0;
    size_t count_ = 0;
    double position_ = 0;
    size_t current_ = 0;
};
