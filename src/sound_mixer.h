// Soundboard : mélangeur de sons. L'interface envoie des ordres (jouer, tout arrêter) ; un thread audio
// mélange les sons en cours dans son propre flux (le micro virtuel, ou le casque). Sans verrou :
// un seul thread envoie des ordres, un seul thread mélange.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

class SoundMixer {
public:
    // Les échantillons doivent rester valides tant que le son peut jouer (le soundboard ne les libère jamais
    // pendant que SoundZ tourne). tag sert à l'interface pour savoir quel bouton est en cours (0..63).
    bool play(const float* samples, int length, float gain, int tag) {
        return send({kPlay, samples, length, gain, tag});
    }
    void stopAll() { send({kStopAll, nullptr, 0, 0, 0}); }
    void setVolume(float v) { volume_.store(v, std::memory_order_relaxed); }

    // À appeler seulement quand aucun thread audio ne mélange (avant d'ouvrir le flux) :
    // oublie les ordres en attente et les sons en cours.
    void resetIdle() {
        tail_.store(head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        for (Voice& v : voices_) v.samples = nullptr;
        playing_.store(0, std::memory_order_relaxed);
    }

    // Sons en cours (un bit par tag), pour l'affichage.
    uint64_t playing() const { return playing_.load(std::memory_order_relaxed); }

    // Thread audio : ajoute les sons en cours à out.
    void mix(float* out, int n) {
        uint32_t t = tail_.load(std::memory_order_relaxed);
        const uint32_t h = head_.load(std::memory_order_acquire);
        for (; t != h; ++t) {
            const Command& c = queue_[t % kQueue];
            if (c.type == kStopAll) {
                for (Voice& v : voices_) v.samples = nullptr;
                continue;
            }
            // Le même son relancé repart du début ; sinon on prend une place libre (ou la plus ancienne).
            Voice* slot = nullptr;
            for (Voice& v : voices_)
                if (v.samples == c.samples) slot = &v;
            for (Voice& v : voices_)
                if (!slot && !v.samples) slot = &v;
            if (!slot) {
                slot = &voices_[0];
                for (Voice& v : voices_)
                    if (v.started < slot->started) slot = &v;
            }
            *slot = Voice{c.samples, c.length, 0, c.gain, c.tag, ++counter_};
        }
        tail_.store(t, std::memory_order_release);

        const float volume = volume_.load(std::memory_order_relaxed);
        uint64_t mask = 0;
        for (Voice& v : voices_) {
            if (!v.samples) continue;
            const float g = v.gain * volume;
            const int count = std::min(n, v.length - v.position);
            const float* s = v.samples + v.position;
            for (int i = 0; i < count; ++i) out[i] += s[i] * g;
            v.position += count;
            if (v.position >= v.length) v.samples = nullptr;
            else mask |= uint64_t(1) << (v.tag & 63);
        }
        playing_.store(mask, std::memory_order_relaxed);
    }

private:
    enum Type { kPlay, kStopAll };
    struct Command {
        Type type;
        const float* samples;
        int length;
        float gain;
        int tag;
    };
    struct Voice {
        const float* samples = nullptr;
        int length = 0, position = 0;
        float gain = 1;
        int tag = 0;
        uint64_t started = 0;
    };
    static constexpr uint32_t kQueue = 64;

    bool send(const Command& c) {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= kQueue) return false;  // personne ne mélange : on jette
        queue_[h % kQueue] = c;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    std::array<Command, kQueue> queue_{};
    std::atomic<uint32_t> head_{0}, tail_{0};
    std::array<Voice, 8> voices_{};
    uint64_t counter_ = 0;
    std::atomic<float> volume_{1.0f};
    std::atomic<uint64_t> playing_{0};
};
