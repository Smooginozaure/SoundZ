// Soundboard : « replay » du son du PC. Garde en permanence les dernières secondes de ce que joue le PC
// (copie numérique « loopback »), dans une mémoire tournante : rien n'est écrit sur le disque.
//
// Deux façons d'écouter :
//  • tous les programmes sauf un (SoundZ lui-même), quelle que soit leur sortie : YouTube, Discord, jeux…
//    (Windows 10 2004 et plus récent) ;
//  • en secours, une seule sortie (la sortie par défaut).
// Windows n'envoie rien quand tout est muet : les silences sont recomptés à l'horloge du PC, pour que
// « les 10 dernières secondes » correspondent vraiment au temps écoulé.
#pragma once

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "miniaudio/miniaudio.h"
#include "process_loopback.h"

class ReplayBuffer {
public:
    static constexpr int kRate = 48000;
    static constexpr int kSeconds = 10;

    ReplayBuffer() : ring_(kRing, 0.0f) { QueryPerformanceFrequency(&frequency_); }
    ~ReplayBuffer() { close(); }

    // Le son de tous les programmes sauf processId (exclude = true), ou seulement de processId (false).
    bool openProcess(DWORD processId, bool exclude) {
        close();
        stopping_ = false;
        lost_ = false;
        anchored_ = false;
        fillGaps_ = true;  // ce mode n'envoie rien pendant les silences : on les recompte à l'horloge
        return process_.start(processId, exclude, onProcessData, this);
    }

    // Le son d'une seule sortie (nullptr : la sortie par défaut). Un flux de silence y est joué en même temps :
    // sinon Windows n'envoie rien quand la sortie est muette, et relivre en retard quand le son revient.
    bool openDevice(ma_context* context, const ma_device_id* device) {
        fillGaps_ = false;
        if (!start(context, loopConfig(device))) return false;
        ma_device_config keep = ma_device_config_init(ma_device_type_playback);
        keep.playback.pDeviceID = device;
        keep.playback.format = ma_format_f32;
        keep.playback.channels = 2;
        keep.sampleRate = kRate;
        keep.dataCallback = onSilence;
        keep.wasapi.noAutoStreamRouting = device ? MA_TRUE : MA_FALSE;
        if (ma_device_init(context, &keep, &keep_) == MA_SUCCESS) {
            keepInit_ = true;
            ma_device_start(&keep_);
        }
        return true;
    }

    void close() {
        stopping_ = true;
        if (loopInit_) ma_device_uninit(&loop_);
        if (keepInit_) ma_device_uninit(&keep_);
        loopInit_ = keepInit_ = false;
        process_.stop();
    }

    bool isOpen() const { return loopInit_ || process_.running(); }
    // Flux interrompu (sortie disparue…).
    bool lost() const { return lost_.load(std::memory_order_relaxed) || process_.failed(); }

    // Les dernières secondes (mono), les plus anciennes d'abord. Ce qui s'est tu depuis le dernier son
    // compte comme du silence à la fin.
    std::vector<float> last(int seconds) const {
        const uint64_t wanted = uint64_t(std::clamp(seconds, 1, kSeconds)) * kRate;
        const uint64_t w = written_.load(std::memory_order_acquire);
        uint64_t silent = 0;
        if (w > 0) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            int64_t since = (now.QuadPart - lastTick_.load(std::memory_order_relaxed)) * kRate / frequency_.QuadPart;
            if (since > kGap) silent = std::min<uint64_t>(uint64_t(since), wanted);
        }
        const uint64_t fromRing = std::min<uint64_t>(w, wanted - silent);
        std::vector<float> out(static_cast<size_t>(fromRing + silent), 0.0f);
        for (uint64_t i = 0; i < fromRing; ++i) out[size_t(i)] = ring_[size_t((w - fromRing + i) & kMask)];
        return out;
    }

private:
    // Un peu plus que 10 s, pour que l'écriture continue pendant qu'on copie ne touche pas ce qu'on lit.
    static constexpr uint64_t kRing = 1 << 19;  // 10,9 s
    static constexpr uint64_t kMask = kRing - 1;
    static constexpr int64_t kGap = kRate / 4;  // un trou de plus d'un quart de seconde est un vrai silence

    ma_device_config loopConfig(const ma_device_id* device) {
        ma_device_config c = ma_device_config_init(ma_device_type_loopback);
        c.capture.pDeviceID = device;
        c.capture.format = ma_format_f32;
        c.capture.channels = 2;
        c.sampleRate = kRate;
        c.dataCallback = onData;
        c.notificationCallback = onNotification;
        c.pUserData = this;
        c.wasapi.noAutoStreamRouting = device ? MA_TRUE : MA_FALSE;
        return c;
    }

    bool start(ma_context* context, const ma_device_config& c) {
        close();
        stopping_ = false;
        lost_ = false;
        anchored_ = false;
        if (ma_device_init(context, &c, &loop_) != MA_SUCCESS) return false;
        loopInit_ = true;
        if (ma_device_start(&loop_) != MA_SUCCESS) {
            close();
            return false;
        }
        return true;
    }

    static void onData(ma_device* device, void*, const void* input, ma_uint32 frames) {
        if (input) static_cast<ReplayBuffer*>(device->pUserData)->feed(static_cast<const float*>(input), frames);
    }

    static void onSilence(ma_device*, void* output, const void*, ma_uint32 frames) {
        std::fill(static_cast<float*>(output), static_cast<float*>(output) + frames * 2, 0.0f);
    }

    static void onProcessData(void* user, const float* stereo, uint32_t frames) {
        static_cast<ReplayBuffer*>(user)->feed(stereo, frames);
    }

    // Thread audio : range un paquet de son (stéréo) dans la mémoire tournante, en mono.
    // Les paquets arrivent parfois par à-coups (jusqu'à ~0,1 s de retard puis plusieurs d'un coup) : on compare
    // donc le son reçu au temps écoulé depuis le début du flux, et seul un retard de plus d'un quart de seconde
    // est un vrai silence (Windows n'envoyait rien), remis à sa place.
    void feed(const float* in, uint32_t frames) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double nowSamples = double(now.QuadPart) * kRate / double(frequency_.QuadPart);
        uint64_t w = written_.load(std::memory_order_relaxed);
        if (!anchored_) {
            anchored_ = true;
            anchorTime_ = nowSamples;
            anchorWritten_ = double(w);
        }
        const double lag = fillGaps_ ? anchorWritten_ + (nowSamples - anchorTime_) - double(w + frames) : 0.0;
        if (lag > double(kGap)) {
            uint64_t gap = uint64_t(std::min(lag, double(kRing / 2)));
            for (uint64_t i = 0; i < gap; ++i) ring_[size_t((w + i) & kMask)] = 0.0f;
            w += gap;
            anchorTime_ = nowSamples;
            anchorWritten_ = double(w + frames);
        } else if (lag < 0) {  // en avance sur l'horloge du PC : on repart d'ici
            anchorTime_ = nowSamples;
            anchorWritten_ = double(w + frames);
        } else {  // petit retard : on le laisse se résorber (les deux horloges dérivent un tout petit peu)
            anchorWritten_ += lag * 0.01;
        }
        for (uint32_t i = 0; i < frames; ++i) ring_[size_t((w + i) & kMask)] = 0.5f * (in[2 * i] + in[2 * i + 1]);
        lastTick_.store(now.QuadPart, std::memory_order_relaxed);
        written_.store(w + frames, std::memory_order_release);
    }

    static void onNotification(const ma_device_notification* n) {
        auto* self = static_cast<ReplayBuffer*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && !self->stopping_) self->lost_ = true;
    }

    std::vector<float> ring_;
    std::atomic<uint64_t> written_{0};
    std::atomic<int64_t> lastTick_{0};  // horloge du PC au dernier son reçu
    LARGE_INTEGER frequency_;
    std::atomic<bool> lost_{false}, stopping_{false};
    ma_device loop_, keep_;
    bool loopInit_ = false, keepInit_ = false;
    std::atomic<bool> fillGaps_{true};
    ProcessLoopback process_;
    // Côté thread audio seulement : repère « horloge du PC / son reçu » du flux en cours.
    bool anchored_ = false;
    double anchorTime_ = 0, anchorWritten_ = 0;
};
