// Moteur de l'onglet Voix : micro -> effet (autotune / vocodeur) -> CABLE-A (micro virtuel des applis)
// et, en option, retour casque (là où l'on écoute).
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"  // OutputDevice (identifiant + nom d'un appareil)
#include "voice_dsp.h"

class SoundMixer;

class VoiceEngine {
public:
    VoiceEngine();
    ~VoiceEngine();

    bool init(std::wstring& error);
    std::vector<OutputDevice> microphones();
    std::vector<OutputDevice> outputs();

    // monitor peut être nul (pas de retour casque possible).
    bool start(const OutputDevice& mic, const OutputDevice& cable, const OutputDevice* monitor, std::wstring& error);
    void stop();
    bool running() const { return running_; }

    // Réglages, modifiables à tout moment depuis l'interface.
    void setMode(int v) { mode_ = v; }
    void setKey(int v) { key_ = v; }
    void setScale(int v) { scale_ = v; }
    void setTranspose(int v) { transpose_ = v; }
    void setRetuneMs(float v) { retune_ = v; }
    void setAmount(float v) { amount_ = v; }
    void setGateDb(float v) { gate_ = v; }
    void setChords(bool v) { chords_ = v; }
    void setMonitor(bool v) { monitor_ = v; }
    // Sons du soundboard ajoutés au micro virtuel, après l'effet. À régler avant start().
    void setSoundMixer(SoundMixer* mixer) { mixer_ = mixer; }

    // Ce que l'interface affiche.
    float detectedMidi() const { return detected_.load(std::memory_order_relaxed); }
    float targetMidi() const { return target_.load(std::memory_order_relaxed); }
    float level() const { return level_.load(std::memory_order_relaxed); }
    uint64_t capturedFrames() const { return captured_.load(std::memory_order_relaxed); }
    uint64_t underruns() const;  // fois où le câble a manqué de son (diagnostic)

    void setLostCallback(void (*callback)(void*), void* user) { lostCallback_ = callback; lostUser_ = user; }

private:
    struct Impl;
    static void onCapture(ma_device* device, void* output, const void* input, ma_uint32 frames);
    static void onCable(ma_device* device, void* output, const void* input, ma_uint32 frames);
    static void onMonitor(ma_device* device, void* output, const void* input, ma_uint32 frames);
    static void onNotification(const ma_device_notification* notification);
    void capture(const float* input, ma_uint32 frames);

    std::unique_ptr<Impl> impl_;
    bool contextReady_ = false;
    bool running_ = false;
    std::atomic<bool> stopping_{false};

    std::atomic<int> mode_{voice::kModeAutotune}, key_{0}, scale_{0}, transpose_{0};
    std::atomic<float> retune_{0}, amount_{1}, gate_{-50};
    std::atomic<bool> chords_{false}, monitor_{false};
    std::atomic<float> detected_{0}, target_{0}, level_{0};
    std::atomic<uint64_t> captured_{0};
    void (*lostCallback_)(void*) = nullptr;
    void* lostUser_ = nullptr;
    SoundMixer* mixer_ = nullptr;
};
