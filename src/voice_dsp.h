// Traitement de la voix : détection de la note, autotune (PSOLA), vocodeur, porte de bruit.
// Tout fonctionne à 48 kHz, en mono, par petits blocs (temps réel, sans allocation pendant le traitement).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace voice {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;

// ---------------------------------------------------------------- gammes

struct ScaleDef {
    int count;
    int steps[12];
};

// Même ordre que l'interface : majeure, mineure, penta majeure, penta mineure, blues, chromatique.
inline constexpr ScaleDef kScales[] = {
    {7, {0, 2, 4, 5, 7, 9, 11}},
    {7, {0, 2, 3, 5, 7, 8, 10}},
    {5, {0, 2, 4, 7, 9}},
    {5, {0, 3, 5, 7, 10}},
    {6, {0, 3, 5, 6, 7, 10}},
    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};
inline constexpr int kScaleCount = int(sizeof(kScales) / sizeof(kScales[0]));

inline double freqToMidi(double f) { return 69.0 + 12.0 * std::log2(f / 440.0); }
inline double midiToFreq(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }

inline bool inScale(int note, int key, int scale) {
    const ScaleDef& s = kScales[scale];
    int pc = ((note - key) % 12 + 12) % 12;
    for (int i = 0; i < s.count; ++i) if (s.steps[i] == pc) return true;
    return false;
}

// Note de la gamme la plus proche de `midi`, avec un peu d'hystérésis pour ne pas hésiter entre deux notes.
inline int nearestNote(double midi, int key, int scale, int current) {
    int best = -1000;
    double bestDist = 1e9;
    for (int n = int(std::floor(midi)) - 7; n <= int(std::ceil(midi)) + 7; ++n) {
        if (!inScale(n, key, scale)) continue;
        double d = std::fabs(midi - n);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    if (current > -1000 && current != best && inScale(current, key, scale) &&
        std::fabs(midi - current) - bestDist < 0.15) {
        return current;
    }
    return best;
}

// Note située `degrees` degrés plus haut dans la gamme (pour les accords : +2 = tierce, +4 = quinte).
inline int scaleStep(int note, int degrees, int key, int scale) {
    int n = note;
    while (degrees > 0) {
        ++n;
        if (inScale(n, key, scale)) --degrees;
    }
    return n;
}

// ---------------------------------------------------------------- détection de la note (YIN)

class PitchDetector {
public:
    static constexpr int kDecimatedRate = kRate / 2;
    static constexpr int kWindow = 768;                    // 32 ms à 24 kHz
    static constexpr int kMaxTau = kDecimatedRate / 70;    // 70 Hz (voix grave)
    static constexpr int kMinTau = kDecimatedRate / 1000;  // 1000 Hz
    static constexpr int kLength = kWindow + kMaxTau + 2;
    static constexpr int kTaps = 31;

    PitchDetector() {
        // Filtre anti-repliement avant de diviser la fréquence d'échantillonnage par 2.
        const double cutoff = 0.2;  // 9,6 kHz
        double sum = 0;
        for (int i = 0; i < kTaps; ++i) {
            double t = i - (kTaps - 1) / 2.0;
            double sinc = t == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * t) / (kPi * t);
            double w = 0.54 - 0.46 * std::cos(2 * kPi * i / (kTaps - 1));
            taps_[size_t(i)] = float(sinc * w);
            sum += taps_[size_t(i)];
        }
        for (auto& t : taps_) t = float(t / sum);
        ring_.assign(kLength, 0.0f);
        frame_.assign(kLength, 0.0f);
        diff_.assign(kMaxTau + 2, 0.0f);
    }

    void push(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            history_[size_t(historyPos_)] = x[i];
            historyPos_ = (historyPos_ + 1) % kTaps;
            if (++phase_ < 2) continue;
            phase_ = 0;
            float acc = 0;
            for (int k = 0; k < kTaps; ++k) acc += taps_[size_t(k)] * history_[size_t((historyPos_ + k) % kTaps)];
            ring_[size_t(ringPos_)] = acc;
            ringPos_ = (ringPos_ + 1) % kLength;
        }
    }

    // Renvoie la fréquence détectée (Hz), ou 0 si le son n'est pas une note (souffle, consonne, silence).
    float analyze() {
        for (int i = 0; i < kLength; ++i) frame_[size_t(i)] = ring_[size_t((ringPos_ + i) % kLength)];
        const float* x = frame_.data();
        double energy = 0;
        for (int j = 0; j < kWindow; ++j) energy += double(x[j]) * x[j];
        if (energy / kWindow < 1e-7) return 0;  // silence (≈ −70 dBFS)

        diff_[0] = 1;
        double running = 0;
        for (int tau = 1; tau <= kMaxTau + 1; ++tau) {
            float d = 0;
            for (int j = 0; j < kWindow; ++j) {
                float v = x[j] - x[j + tau];
                d += v * v;
            }
            running += d;
            diff_[size_t(tau)] = running > 0 ? float(d * tau / running) : 1.0f;  // CMNDF
        }
        int tau = -1;
        for (int t = kMinTau; t <= kMaxTau; ++t) {
            if (diff_[size_t(t)] < 0.15f) {
                while (t + 1 <= kMaxTau && diff_[size_t(t + 1)] < diff_[size_t(t)]) ++t;
                tau = t;
                break;
            }
        }
        if (tau < 0) {
            int best = kMinTau;
            for (int t = kMinTau; t <= kMaxTau; ++t) if (diff_[size_t(t)] < diff_[size_t(best)]) best = t;
            if (diff_[size_t(best)] > 0.3f) return 0;
            tau = best;
        }
        // Interpolation parabolique pour une précision bien meilleure qu'un échantillon.
        double refined = tau;
        if (tau > kMinTau && tau < kMaxTau) {
            double a = diff_[size_t(tau - 1)], b = diff_[size_t(tau)], c = diff_[size_t(tau + 1)];
            double denom = a - 2 * b + c;
            if (std::fabs(denom) > 1e-12) refined = tau + 0.5 * (a - c) / denom;
        }
        return float(kDecimatedRate / refined);
    }

private:
    std::array<float, kTaps> taps_{};
    std::array<float, kTaps> history_{};
    int historyPos_ = 0;
    int phase_ = 0;
    std::vector<float> ring_, frame_, diff_;
    int ringPos_ = 0;
};

// ---------------------------------------------------------------- changement de hauteur (TD-PSOLA)
//
// La voix est découpée en « grains » de deux périodes, centrés sur chaque période de la note chantée,
// puis recollés plus serrés (plus aigu) ou plus espacés (plus grave). Le timbre est préservé.

class Psola {
public:
    static constexpr int kSize = 8192;           // tampons circulaires (puissance de 2)
    static constexpr int kMaxPeriod = 48000 / 70;
    static constexpr int kDelay = 1800;          // 37,5 ms : > 2,5 périodes de la note la plus grave
    static constexpr int kUnvoicedHalf = 240;    // grains de 10 ms quand ce n'est pas une note

    Psola() {
        in_.assign(kSize, 0.0f);
        acc_.assign(kSize, 0.0f);
    }

    // period : période de la note chantée en échantillons (0 = pas une note) ; ratio : 2 = une octave plus haut.
    void process(const float* x, float* out, float* dryOut, int n, double period, double ratio) {
        for (int i = 0; i < n; ++i) in_[size_t((written_ + i) & (kSize - 1))] = x[i];
        written_ += n;

        bool voiced = period > 0;
        double P = voiced ? std::min<double>(period, kMaxPeriod) : kUnvoicedHalf;
        double r = voiced ? std::clamp(ratio, 0.5, 2.0) : 1.0;
        int half = int(std::lround(P));
        for (;;) {
            long long ai;
            if (voiced) {
                if (!markValid_) { mark_ = std::llround(synth_); markValid_ = true; }
                // Repère d'analyse le plus proche, recalé sur les vraies vibrations de la voix.
                bool ready = true;
                while (double(mark_) + P * 0.5 < synth_) {
                    if (!advanceMark(half)) { ready = false; break; }
                }
                if (!ready) break;
                ai = mark_;
            } else {
                markValid_ = false;
                ai = std::llround(synth_);
            }
            long long si = std::llround(synth_);
            if (ai + half + 1 > written_) break;
            // Grains plus serrés = plus de recouvrement : on compense (en puissance).
            float scale = float(1.0 / std::sqrt(r));
            for (int j = -half; j <= half; ++j) {
                float w = 0.5f + 0.5f * std::cos(float(kPi) * float(j) / float(half));
                acc_[size_t((si + j) & (kSize - 1))] += in_[size_t((ai + j) & (kSize - 1))] * w * scale;
            }
            synth_ += P / r;
        }

        long long start = written_ - n - kDelay;
        for (int i = 0; i < n; ++i) {
            long long idx = start + i;
            size_t k = size_t(idx & (kSize - 1));
            out[i] = idx >= 0 ? acc_[k] : 0.0f;
            dryOut[i] = idx >= 0 ? in_[k] : 0.0f;  // voix d'origine, avec le même retard
            acc_[k] = 0;
        }
    }

private:
    float at(long long i) const { return in_[size_t(i & (kSize - 1))]; }

    // Repère suivant : environ une période plus loin, à l'endroit où la forme d'onde ressemble le plus
    // à celle du repère actuel. Les grains suivent ainsi le vibrato et les glissements de la voix.
    bool advanceMark(int period) {
        const int range = std::max(2, period / 5);
        const int half = period / 2;
        long long predicted = mark_ + period;
        if (predicted + range + half + 1 > written_) return false;
        double best = -1e30;
        long long bestPos = predicted;
        for (long long c = predicted - range; c <= predicted + range; ++c) {
            double dot = 0, energy = 1e-9;
            for (int j = -half; j < half; ++j) {
                float v = at(c + j);
                dot += double(at(mark_ + j)) * v;
                energy += double(v) * v;
            }
            double score = dot / std::sqrt(energy);
            if (score > best) { best = score; bestPos = c; }
        }
        mark_ = bestPos;
        return true;
    }

    std::vector<float> in_, acc_;
    long long written_ = 0;
    double synth_ = 0;
    long long mark_ = 0;
    bool markValid_ = false;
};

// ---------------------------------------------------------------- vocodeur

class Vocoder {
public:
    static constexpr int kBands = 20;
    static constexpr int kVoices = 3;

    Vocoder() {
        const double low = 120, high = 7000, q = 3.0;
        for (int b = 0; b < kBands; ++b) {
            double f = low * std::pow(high / low, double(b) / (kBands - 1));
            bands_[size_t(b)].configure(f, q);
            noise_[size_t(b)] = f > 3500 ? 0.6f : (f > 2000 ? 0.25f : 0.0f);  // consonnes (s, ch, t)
        }
        double atk = 0.003, rel = 0.025;
        attack_ = float(1 - std::exp(-1.0 / (atk * kRate)));
        release_ = float(1 - std::exp(-1.0 / (rel * kRate)));
        glide_ = float(1 - std::exp(-1.0 / (0.015 * kRate)));
    }

    // Notes jouées par le synthé (MIDI). count = 1 (voix seule) ou 3 (accord).
    void setNotes(const double* midi, int count) {
        count_ = std::clamp(count, 1, kVoices);
        for (int v = 0; v < count_; ++v) target_[size_t(v)] = float(midiToFreq(midi[v]));
    }

    void process(const float* mod, float* out, int n) {
        const float voiceGain = 1.0f / std::sqrt(float(count_));
        for (int i = 0; i < n; ++i) {
            // Synthé : dents de scie sans repliement (PolyBLEP), qui glissent vers la note visée.
            float saw = 0;
            for (int v = 0; v < count_; ++v) {
                float& f = freq_[size_t(v)];
                if (f <= 0) f = target_[size_t(v)];
                f += (target_[size_t(v)] - f) * glide_;
                double dt = f / kRate;
                double& ph = phase_[size_t(v)];
                ph += dt;
                if (ph >= 1) ph -= 1;
                saw += float(2 * ph - 1 - polyBlep(ph, dt));
            }
            saw *= voiceGain;
            seed_ = seed_ * 1664525u + 1013904223u;
            float white = float(int32_t(seed_)) * (1.0f / 2147483648.0f);

            float m = mod[i];
            float y = 0;
            for (int b = 0; b < kBands; ++b) {
                Band& band = bands_[size_t(b)];
                float mb = band.mod.run(m);
                float cb = band.car.run(saw + white * noise_[size_t(b)]);
                follow(band.envMod, std::fabs(mb));
                follow(band.envCar, std::fabs(cb));
                // Le synthé prend l'enveloppe de la voix, bande par bande.
                y += cb * (band.envMod / (band.envCar + 1e-4f));
            }
            out[i] = y * kMakeup;
        }
    }

private:
    static constexpr float kMakeup = 1.45f;

    struct Biquad {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1a = 0, z2a = 0, z1b = 0, z2b = 0;
        void configure(double f, double q) {
            double w = 2 * kPi * f / kRate, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
            b0 = float(alpha / a0);
            b2 = float(-alpha / a0);
            a1 = float(-2 * std::cos(w) / a0);
            a2 = float((1 - alpha) / a0);
        }
        // Deux passe-bande identiques en série : bande plus nette.
        float run(float x) {
            float y = b0 * x + z1a;
            z1a = -a1 * y + z2a;
            z2a = b2 * x - a2 * y;
            float y2 = b0 * y + z1b;
            z1b = -a1 * y2 + z2b;
            z2b = b2 * y - a2 * y2;
            return y2;
        }
    };
    struct Band {
        Biquad mod, car;
        float envMod = 0, envCar = 0;
        void configure(double f, double q) { mod.configure(f, q); car.configure(f, q); }
    };

    void follow(float& env, float v) const { env += (v - env) * (v > env ? attack_ : release_); }

    static double polyBlep(double t, double dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1; }
        if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
        return 0;
    }

    std::array<Band, kBands> bands_{};
    std::array<float, kBands> noise_{};
    std::array<float, kVoices> target_{{220, 277, 330}};
    std::array<float, kVoices> freq_{};
    std::array<double, kVoices> phase_{};
    int count_ = 1;
    float attack_ = 0, release_ = 0, glide_ = 0;
    uint32_t seed_ = 12345;
};

// ---------------------------------------------------------------- porte de bruit

class NoiseGate {
public:
    void setThreshold(float db) { threshold_ = db <= -79.5f ? 0.0f : std::pow(10.0f, db / 20.0f); }

    // Renvoie le niveau du micro (pour l'indicateur) et applique la porte sur place.
    float process(float* x, int n) {
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            float a = std::fabs(x[i]);
            peak = std::max(peak, a);
            env_ += (a - env_) * (a > env_ ? 0.05f : 0.0005f);
            bool open = env_ > threshold_;
            if (open) hold_ = kRate / 10;  // reste ouverte 100 ms après la dernière syllabe
            else if (hold_ > 0) --hold_;
            float target = (open || hold_ > 0) ? 1.0f : 0.0f;
            gain_ += (target - gain_) * (target > gain_ ? 0.02f : 0.0008f);
            x[i] *= gain_;
        }
        return peak;
    }

    bool isOpen() const { return gain_ > 0.5f; }

private:
    float threshold_ = 0;
    float env_ = 0, gain_ = 1;
    int hold_ = 0;
};

// ---------------------------------------------------------------- chaîne complète

enum Mode { kModeNone = 0, kModeAutotune = 1, kModeVocoder = 2 };

struct Settings {
    int mode = kModeAutotune;
    int key = 0;          // 0 = Do … 11 = Si
    int scale = 0;        // index dans kScales
    int transpose = 0;    // demi-tons, −12 … +12
    float retuneMs = 0;   // 0 = correction instantanée (effet robot)
    float amount = 1;     // 0 … 1
    float gateDb = -50;   // −80 = porte désactivée
    bool chords = false;  // vocodeur : accord tierce + quinte
};

struct Readout {
    float detectedMidi = 0;  // 0 = pas de note
    float targetMidi = 0;
    float level = 0;         // crête du micro, 0 … 1
};

class VoiceProcessor {
public:
    static constexpr int kChunk = 128;
    static constexpr int kHop = 256;

    void process(const float* input, float* output, int n, const Settings& s) {
        gate_.setThreshold(s.gateDb);
        for (int pos = 0; pos < n; pos += kChunk) {
            int len = std::min(kChunk, n - pos);
            float* x = gated_.data();
            std::copy(input + pos, input + pos + len, x);
            float peak = gate_.process(x, len);
            readout_.level = std::max(peak, readout_.level * 0.97f);

            detector_.push(x, len);
            sinceAnalysis_ += len;
            if (sinceAnalysis_ >= kHop) {
                sinceAnalysis_ = 0;
                analyze(s);
            }

            float* tuned = tuned_.data();
            float* dry = dry_.data();
            psola_.process(x, tuned, dry, len, period_, ratio_);
            float* robot = robot_.data();
            vocoder_.process(dry, robot, len);

            // Une voix transposée de beaucoup perd un peu de volume (moins d'harmoniques) :
            // on ramène doucement son niveau à celui de la voix d'origine.
            for (int i = 0; i < len; ++i) {
                powerDry_ += (dry[i] * dry[i] - powerDry_) * kLevelSpeed;
                powerTuned_ += (tuned[i] * tuned[i] - powerTuned_) * kLevelSpeed;
            }
            float wanted = powerTuned_ > 1e-9f ? std::sqrt(powerDry_ / powerTuned_) : 1.0f;
            wanted = std::clamp(wanted, 0.7f, 2.5f);
            for (int i = 0; i < len; ++i) {
                levelGain_ += (wanted - levelGain_) * 0.0005f;
                tuned[i] *= levelGain_;
            }

            // Passage en douceur d'un mode à l'autre (20 ms).
            for (int i = 0; i < len; ++i) {
                for (int m = 0; m < 3; ++m) {
                    float target = m == s.mode ? 1.0f : 0.0f;
                    mix_[size_t(m)] += (target - mix_[size_t(m)]) * 0.0010f;
                }
                output[pos + i] = dry[i] * mix_[0] + tuned[i] * mix_[1] + robot[i] * mix_[2];
            }
        }
    }

    const Readout& readout() const { return readout_; }

private:
    void analyze(const Settings& s) {
        float f = gate_.isOpen() ? detector_.analyze() : 0.0f;
        // Médiane des 3 dernières mesures : élimine les erreurs ponctuelles (sauts d'octave).
        history_[0] = history_[1];
        history_[1] = history_[2];
        history_[2] = f;
        float a = history_[0], b = history_[1], c = history_[2];
        float med = std::max(std::min(a, b), std::min(std::max(a, b), c));
        if (f <= 0 || med <= 0) {
            period_ = 0;
            readout_.detectedMidi = 0;
            readout_.targetMidi = 0;
            // Le vocodeur garde sa dernière note.
            return;
        }
        double midi = freqToMidi(med);
        note_ = nearestNote(midi, s.key, s.scale, note_);
        double correction = note_ - midi;
        if (s.retuneMs < 1) {
            smoothed_ = correction;
        } else {
            double hopMs = 1000.0 * kHop / kRate;
            smoothed_ += (correction - smoothed_) * (1 - std::exp(-hopMs / s.retuneMs));
        }
        double shift = s.amount * smoothed_ + s.transpose;
        period_ = kRate / med;
        ratio_ = std::pow(2.0, shift / 12.0);
        readout_.detectedMidi = float(midi);
        readout_.targetMidi = float(midi + shift);

        double notes[3] = {double(note_ + s.transpose), 0, 0};
        int count = 1;
        if (s.chords) {
            notes[1] = scaleStep(note_, 2, s.key, s.scale) + s.transpose;
            notes[2] = scaleStep(note_, 4, s.key, s.scale) + s.transpose;
            count = 3;
        }
        vocoder_.setNotes(notes, count);
    }

    PitchDetector detector_;
    Psola psola_;
    Vocoder vocoder_;
    NoiseGate gate_;
    std::array<float, kChunk> gated_{}, tuned_{}, dry_{}, robot_{};
    static constexpr float kLevelSpeed = 1.0f / (0.3f * kRate);  // mesure du niveau sur ~300 ms
    float powerDry_ = 0, powerTuned_ = 0, levelGain_ = 1;
    std::array<float, 3> mix_{{0, 1, 0}};
    std::array<float, 3> history_{};
    int sinceAnalysis_ = 0;
    int note_ = -1000;
    double smoothed_ = 0;
    double period_ = 0;
    double ratio_ = 1;
    Readout readout_;
};

}  // namespace voice
