#include "engine.h"

#include <algorithm>
#include <cstring>

#include "transposer.h"
#include "util.h"

namespace {
constexpr ma_uint32 kMaxChunk = 2048;                    // taille max traitée d'un coup
constexpr ma_uint32 kMaxInput = kMaxChunk + kMaxChunk / 50 + 4;
constexpr ma_uint32 kRingFrames = PitchEngine::kSampleRate;  // 1 s de réserve
constexpr double kTargetFill = 1440;                    // 30 ms d'avance dans la réserve
constexpr double kMaxFill = kTargetFill * 4 + 4800;      // au-delà : on rattrape d'un coup
constexpr double kMaxDrift = 0.005;                      // correction d'horloge max : ±0,5 %
}  // namespace

struct PitchEngine::Impl {
    ma_context context;
    ma_device captureDevice;
    ma_device playbackDevice;
    bool captureInit = false;
    bool playbackInit = false;
    ma_pcm_rb ring;
    bool ringInit = false;

    Transposer<kChannels> transposer;
    std::vector<float> in[kChannels];
    std::vector<float> out[kChannels];

    // État du thread de lecture uniquement.
    int appliedSemitones = 0;
    float appliedGain = 1;
    bool flowing = false;
    double fill = kTargetFill;
    double fraction = 0;
};

PitchEngine::PitchEngine() : impl_(new Impl()) {}

PitchEngine::~PitchEngine() {
    stop();
    if (contextReady_) ma_context_uninit(&impl_->context);
}

bool PitchEngine::init(std::wstring& error) {
    if (contextReady_) return true;
    ma_backend backends[] = {ma_backend_wasapi};
    ma_context_config config = ma_context_config_init();
    if (ma_context_init(backends, 1, &config, &impl_->context) != MA_SUCCESS) {
        error = L"Impossible d'accéder au son de Windows (WASAPI).";
        return false;
    }
    contextReady_ = true;
    return true;
}

std::vector<OutputDevice> PitchEngine::outputs() {
    std::vector<OutputDevice> list;
    if (!contextReady_) return list;
    ma_device_info* playback = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(&impl_->context, &playback, &playbackCount, nullptr, nullptr) != MA_SUCCESS) {
        return list;
    }
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        OutputDevice device;
        device.id = playback[i].id;
        device.name = widen(playback[i].name);
        device.endpoint = reinterpret_cast<const wchar_t*>(playback[i].id.wasapi);
        list.push_back(device);
    }
    return list;
}

bool PitchEngine::start(const OutputDevice& sas, const OutputDevice& out, std::wstring& error) {
    stop();
    Impl& m = *impl_;

    if (ma_pcm_rb_init(ma_format_f32, kChannels, kRingFrames, nullptr, nullptr, &m.ring) != MA_SUCCESS) {
        error = L"Mémoire insuffisante.";
        return false;
    }
    m.ringInit = true;

    m.transposer.configure(int(kMaxChunk));
    m.appliedSemitones = semitones_.load();
    m.transposer.setSemitones(m.appliedSemitones);
    for (ma_uint32 c = 0; c < kChannels; ++c) {
        m.in[c].assign(kMaxInput, 0.0f);
        m.out[c].assign(kMaxChunk, 0.0f);
    }
    m.appliedGain = gain_.load();
    m.flowing = false;
    m.fill = kTargetFill;
    m.fraction = 0;
    captured_ = 0;
    underruns_ = 0;
    stopping_ = false;

    ma_device_config play = ma_device_config_init(ma_device_type_playback);
    play.playback.pDeviceID = &out.id;
    play.playback.format = ma_format_f32;
    play.playback.channels = kChannels;
    play.sampleRate = kSampleRate;
    play.periodSizeInMilliseconds = 10;
    play.performanceProfile = ma_performance_profile_low_latency;
    play.dataCallback = &PitchEngine::onPlayback;
    play.notificationCallback = &PitchEngine::onNotification;
    play.pUserData = this;
    play.wasapi.noAutoStreamRouting = MA_TRUE;
    if (ma_device_init(&m.context, &play, &m.playbackDevice) != MA_SUCCESS) {
        error = L"Impossible d'ouvrir la sortie « " + out.name + L" ».";
        stop();
        return false;
    }
    m.playbackInit = true;

    ma_device_config cap = ma_device_config_init(ma_device_type_loopback);
    cap.capture.pDeviceID = &sas.id;  // en loopback, on désigne la SORTIE à écouter
    cap.capture.format = ma_format_f32;
    cap.capture.channels = kChannels;
    cap.sampleRate = kSampleRate;
    cap.periodSizeInMilliseconds = 10;
    cap.performanceProfile = ma_performance_profile_low_latency;
    cap.dataCallback = &PitchEngine::onCapture;
    cap.notificationCallback = &PitchEngine::onNotification;
    cap.pUserData = this;
    cap.wasapi.noAutoStreamRouting = MA_TRUE;
    if (ma_device_init(&m.context, &cap, &m.captureDevice) != MA_SUCCESS) {
        error = L"Impossible d'écouter le sas « " + sas.name + L" ».";
        stop();
        return false;
    }
    m.captureInit = true;

    if (ma_device_start(&m.captureDevice) != MA_SUCCESS || ma_device_start(&m.playbackDevice) != MA_SUCCESS) {
        error = L"Impossible de démarrer le son.";
        stop();
        return false;
    }
    running_ = true;
    return true;
}

void PitchEngine::stop() {
    Impl& m = *impl_;
    stopping_ = true;
    if (m.captureInit) { ma_device_uninit(&m.captureDevice); m.captureInit = false; }
    if (m.playbackInit) { ma_device_uninit(&m.playbackDevice); m.playbackInit = false; }
    if (m.ringInit) { ma_pcm_rb_uninit(&m.ring); m.ringInit = false; }
    running_ = false;
}

int PitchEngine::processingLatencyMs() const {
    double frames = impl_->transposer.latencyFrames() + kTargetFill;
    return int(frames * 1000.0 / kSampleRate + 0.5);
}

void PitchEngine::onCapture(ma_device* device, void*, const void* input, ma_uint32 frames) {
    static_cast<PitchEngine*>(device->pUserData)->capture(static_cast<const float*>(input), frames);
}

void PitchEngine::onPlayback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    static_cast<PitchEngine*>(device->pUserData)->playback(static_cast<float*>(output), frames);
}

void PitchEngine::onNotification(const ma_device_notification* notification) {
    if (notification->type != ma_device_notification_type_stopped) return;
    auto* self = static_cast<PitchEngine*>(notification->pDevice->pUserData);
    // Arrêt que nous n'avons pas demandé : la sortie a disparu (débranchée, désactivée…).
    if (!self->stopping_.exchange(true) && self->lostCallback_) self->lostCallback_(self->lostUser_);
}

void PitchEngine::capture(const float* input, ma_uint32 frames) {
    captured_.fetch_add(frames, std::memory_order_relaxed);
    ma_pcm_rb* ring = &impl_->ring;
    while (frames > 0) {
        ma_uint32 count = frames;
        void* dst = nullptr;
        if (ma_pcm_rb_acquire_write(ring, &count, &dst) != MA_SUCCESS || count == 0) break;  // réserve pleine
        std::memcpy(dst, input, size_t(count) * kChannels * sizeof(float));
        ma_pcm_rb_commit_write(ring, count);
        input += size_t(count) * kChannels;
        frames -= count;
    }
}

void PitchEngine::playback(float* output, ma_uint32 frames) {
    while (frames > 0) {
        ma_uint32 n = std::min(frames, kMaxChunk);
        renderChunk(output, n);
        output += size_t(n) * kChannels;
        frames -= n;
    }
}

// Lit `frames` échantillons de la réserve dans les tampons d'entrée, à partir de `offset`.
ma_uint32 PitchEngine::readInput(ma_uint32 frames, ma_uint32 offset) {
    Impl& m = *impl_;
    ma_uint32 got = 0;
    while (got < frames) {
        ma_uint32 count = frames - got;
        void* src = nullptr;
        if (ma_pcm_rb_acquire_read(&m.ring, &count, &src) != MA_SUCCESS || count == 0) break;
        const float* f = static_cast<const float*>(src);
        for (ma_uint32 i = 0; i < count; ++i) {
            m.in[0][offset + got + i] = f[2 * i];
            m.in[1][offset + got + i] = f[2 * i + 1];
        }
        ma_pcm_rb_commit_read(&m.ring, count);
        got += count;
    }
    return got;
}

void PitchEngine::renderChunk(float* output, ma_uint32 n) {
    Impl& m = *impl_;

    int semitones = semitones_.load(std::memory_order_relaxed);
    if (semitones != m.appliedSemitones) {
        m.transposer.setSemitones(semitones);
        m.appliedSemitones = semitones;
    }

    double available = double(ma_pcm_rb_available_read(&m.ring));
    if (!m.flowing && available >= kTargetFill + n) {
        // Le son arrive (à nouveau) : on démarre avec la bonne avance.
        m.flowing = true;
        m.fill = available;
        m.fraction = 0;
    }

    ma_uint32 inputCount = n;  // sans son entrant, on nourrit le traitement de silence
    ma_uint32 got = 0;
    if (m.flowing) {
        if (available > kMaxFill) {
            // Trop de retard accumulé (à-coup du système) : on saute directement au présent.
            ma_pcm_rb_seek_read(&m.ring, ma_uint32(available - kTargetFill));
            available = kTargetFill;
            m.fill = kTargetFill;
        }
        // Les horloges du sas et de la G8 ne sont jamais parfaitement identiques :
        // on consomme un poil plus ou un poil moins d'entrée pour garder une avance constante.
        m.fill += 0.02 * (available - m.fill);
        double drift = std::clamp((m.fill - kTargetFill) / kTargetFill * kMaxDrift, -kMaxDrift, kMaxDrift);
        double wanted = n * (1.0 + drift) + m.fraction;
        inputCount = std::min<ma_uint32>(ma_uint32(wanted), kMaxInput);
        m.fraction = wanted - double(inputCount);

        got = readInput(std::min<ma_uint32>(inputCount, ma_uint32(available)), 0);
        if (got < inputCount) {
            // Plus de son entrant : on complète avec du silence et on attendra la reprise.
            m.flowing = false;
            underruns_.fetch_add(1, std::memory_order_relaxed);
            inputCount = std::max(got, n);
        }
    }
    for (ma_uint32 c = 0; c < kChannels; ++c) {
        std::fill(m.in[c].begin() + got, m.in[c].begin() + inputCount, 0.0f);
    }

    float* ins[kChannels] = {m.in[0].data(), m.in[1].data()};
    float* outs[kChannels] = {m.out[0].data(), m.out[1].data()};
    m.transposer.process(ins, int(inputCount), outs, int(n));

    // Volume : transition progressive sur le bloc pour éviter les clics.
    const float from = m.appliedGain;
    const float to = gain_.load(std::memory_order_relaxed);
    const float step = (to - from) / float(n);
    for (ma_uint32 i = 0; i < n; ++i) {
        float g = from + step * float(i + 1);
        output[2 * i] = m.out[0][i] * g;
        output[2 * i + 1] = m.out[1][i] * g;
    }
    m.appliedGain = to;
}
