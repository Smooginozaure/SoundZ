// Moteur de transposition : capture en "loopback" la sortie qui sert de sas,
// transpose le son, puis le joue sur la vraie sortie (ton casque ou tes enceintes).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "miniaudio/miniaudio.h"

struct OutputDevice {
    ma_device_id id;
    std::wstring name;     // nom lisible, ex. « Haut-parleurs (Sound Blaster G8 USB-1) »
    std::wstring endpoint; // identifiant Windows de la sortie (sert aussi pour la sortie par défaut)
};

class PitchEngine {
public:
    static constexpr ma_uint32 kSampleRate = 48000;
    static constexpr ma_uint32 kChannels = 2;
    static constexpr int kMinSemitones = -4;
    static constexpr int kMaxSemitones = 4;

    PitchEngine();
    ~PitchEngine();

    bool init(std::wstring& error);
    std::vector<OutputDevice> outputs();

    bool start(const OutputDevice& sas, const OutputDevice& out, std::wstring& error);
    void stop();
    bool running() const { return running_; }

    void setSemitones(int semitones) { semitones_.store(semitones, std::memory_order_relaxed); }
    // Volume appliqué au son transposé (recopie du volume Windows du sas), 0..1.
    void setGain(float gain) { gain_.store(gain, std::memory_order_relaxed); }

    // Nombre total d'échantillons reçus depuis le sas (pour savoir si de la musique arrive).
    uint64_t capturedFrames() const { return captured_.load(std::memory_order_relaxed); }
    // Nombre de fois où le son entrant a manqué (coupure ou fin de morceau).
    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    // Latence ajoutée par le traitement (hors tampons de Windows), en millisecondes.
    int processingLatencyMs() const;

    // Appelé (depuis un thread audio) si une sortie disparaît pendant le fonctionnement.
    void setLostCallback(void (*callback)(void*), void* user) { lostCallback_ = callback; lostUser_ = user; }

private:
    struct Impl;

    static void onCapture(ma_device* device, void* output, const void* input, ma_uint32 frames);
    static void onPlayback(ma_device* device, void* output, const void* input, ma_uint32 frames);
    static void onNotification(const ma_device_notification* notification);

    void capture(const float* input, ma_uint32 frames);
    void playback(float* output, ma_uint32 frames);
    void renderChunk(float* output, ma_uint32 frames);
    ma_uint32 readInput(ma_uint32 frames, ma_uint32 offset);

    std::unique_ptr<Impl> impl_;
    bool contextReady_ = false;
    bool running_ = false;
    std::atomic<bool> stopping_{false};
    std::atomic<int> semitones_{0};
    std::atomic<float> gain_{1.0f};
    std::atomic<uint64_t> captured_{0};
    std::atomic<uint64_t> underruns_{0};
    void (*lostCallback_)(void*) = nullptr;
    void* lostUser_ = nullptr;
};
