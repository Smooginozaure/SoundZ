#include "voice_engine.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "elastic.h"
#include "sound_mixer.h"
#include "util.h"

namespace {
constexpr ma_uint32 kRate = voice::kRate;
constexpr ma_uint32 kMaxBlock = 4096;
}  // namespace

struct VoiceEngine::Impl {
    ma_context context;
    ma_device mic, cable, monitor;
    bool micInit = false, cableInit = false, monitorInit = false;
    voice::VoiceProcessor processor;
    ElasticBuffer toCable{1440};   // 30 ms d'avance : couvre un appel audio en retard d'une période
    ElasticBuffer toMonitor{1440};
    std::vector<float> processed = std::vector<float>(kMaxBlock);
    std::vector<float> silence = std::vector<float>(kMaxBlock, 0.0f);
};

VoiceEngine::VoiceEngine() : impl_(new Impl()) {}

VoiceEngine::~VoiceEngine() {
    stop();
    if (contextReady_) ma_context_uninit(&impl_->context);
}

bool VoiceEngine::init(std::wstring& error) {
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

static std::vector<OutputDevice> listDevices(ma_context* context, bool capture) {
    std::vector<OutputDevice> list;
    ma_device_info *playback = nullptr, *recording = nullptr;
    ma_uint32 playbackCount = 0, recordingCount = 0;
    if (ma_context_get_devices(context, &playback, &playbackCount, &recording, &recordingCount) != MA_SUCCESS) return list;
    ma_device_info* infos = capture ? recording : playback;
    ma_uint32 count = capture ? recordingCount : playbackCount;
    for (ma_uint32 i = 0; i < count; ++i) {
        OutputDevice d;
        d.id = infos[i].id;
        d.name = widen(infos[i].name);
        d.endpoint = reinterpret_cast<const wchar_t*>(infos[i].id.wasapi);
        list.push_back(d);
    }
    return list;
}

std::vector<OutputDevice> VoiceEngine::microphones() {
    return contextReady_ ? listDevices(&impl_->context, true) : std::vector<OutputDevice>();
}

std::vector<OutputDevice> VoiceEngine::outputs() {
    return contextReady_ ? listDevices(&impl_->context, false) : std::vector<OutputDevice>();
}

bool VoiceEngine::start(const OutputDevice& mic, const OutputDevice& cable, const OutputDevice* monitor, std::wstring& error) {
    stop();
    Impl& m = *impl_;
    m.processor = voice::VoiceProcessor();
    m.toCable.reset();
    m.toMonitor.reset();
    captured_ = 0;
    stopping_ = false;
    if (mixer_) mixer_->resetIdle();  // aucun flux ouvert à ce moment : pas de vieux sons en attente

    auto playbackConfig = [this](const OutputDevice& device, ma_device_data_proc callback) {
        ma_device_config c = ma_device_config_init(ma_device_type_playback);
        c.playback.pDeviceID = &device.id;
        c.playback.format = ma_format_f32;
        c.playback.channels = 1;  // la voix est mono ; Windows la recopie sur les deux côtés
        c.sampleRate = kRate;
        c.periodSizeInMilliseconds = 10;
        c.performanceProfile = ma_performance_profile_low_latency;
        c.dataCallback = callback;
        c.notificationCallback = &VoiceEngine::onNotification;
        c.pUserData = this;
        c.wasapi.noAutoStreamRouting = MA_TRUE;
        return c;
    };

    ma_device_config cableConfig = playbackConfig(cable, &VoiceEngine::onCable);
    if (ma_device_init(&m.context, &cableConfig, &m.cable) != MA_SUCCESS) {
        error = L"Impossible d'ouvrir « " + cable.name + L" ».";
        stop();
        return false;
    }
    m.cableInit = true;

    if (monitor) {
        ma_device_config monitorConfig = playbackConfig(*monitor, &VoiceEngine::onMonitor);
        if (ma_device_init(&m.context, &monitorConfig, &m.monitor) == MA_SUCCESS) m.monitorInit = true;
    }

    ma_device_config micConfig = ma_device_config_init(ma_device_type_capture);
    micConfig.capture.pDeviceID = &mic.id;
    micConfig.capture.format = ma_format_f32;
    micConfig.capture.channels = 1;
    micConfig.sampleRate = kRate;
    micConfig.periodSizeInMilliseconds = 10;
    micConfig.performanceProfile = ma_performance_profile_low_latency;
    micConfig.dataCallback = &VoiceEngine::onCapture;
    micConfig.notificationCallback = &VoiceEngine::onNotification;
    micConfig.pUserData = this;
    micConfig.wasapi.noAutoStreamRouting = MA_TRUE;
    if (ma_device_init(&m.context, &micConfig, &m.mic) != MA_SUCCESS) {
        error = L"Impossible d'ouvrir le micro « " + mic.name + L" ».";
        stop();
        return false;
    }
    m.micInit = true;

    bool ok = ma_device_start(&m.cable) == MA_SUCCESS && ma_device_start(&m.mic) == MA_SUCCESS;
    if (ok && m.monitorInit) ok = ma_device_start(&m.monitor) == MA_SUCCESS;
    if (!ok) {
        error = L"Impossible de démarrer la voix.";
        stop();
        return false;
    }
    running_ = true;
    return true;
}

void VoiceEngine::stop() {
    Impl& m = *impl_;
    stopping_ = true;
    if (m.micInit) { ma_device_uninit(&m.mic); m.micInit = false; }
    if (m.cableInit) { ma_device_uninit(&m.cable); m.cableInit = false; }
    if (m.monitorInit) { ma_device_uninit(&m.monitor); m.monitorInit = false; }
    running_ = false;
    detected_ = 0;
    target_ = 0;
    level_ = 0;
}

void VoiceEngine::onCapture(ma_device* device, void*, const void* input, ma_uint32 frames) {
    static_cast<VoiceEngine*>(device->pUserData)->capture(static_cast<const float*>(input), frames);
}

void VoiceEngine::onCable(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<VoiceEngine*>(device->pUserData);
    self->impl_->toCable.pull(static_cast<float*>(output), int(frames));
}

void VoiceEngine::onMonitor(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<VoiceEngine*>(device->pUserData);
    self->impl_->toMonitor.pull(static_cast<float*>(output), int(frames));
}

void VoiceEngine::onNotification(const ma_device_notification* notification) {
    if (notification->type != ma_device_notification_type_stopped) return;
    auto* self = static_cast<VoiceEngine*>(notification->pDevice->pUserData);
    if (!self->stopping_.exchange(true) && self->lostCallback_) self->lostCallback_(self->lostUser_);
}

void VoiceEngine::capture(const float* input, ma_uint32 frames) {
    Impl& m = *impl_;
    captured_.fetch_add(frames, std::memory_order_relaxed);
    voice::Settings s;
    s.mode = mode_.load(std::memory_order_relaxed);
    s.key = key_.load(std::memory_order_relaxed);
    s.scale = std::clamp(scale_.load(std::memory_order_relaxed), 0, voice::kScaleCount - 1);
    s.transpose = transpose_.load(std::memory_order_relaxed);
    s.retuneMs = retune_.load(std::memory_order_relaxed);
    s.amount = amount_.load(std::memory_order_relaxed);
    s.gateDb = gate_.load(std::memory_order_relaxed);
    s.chords = chords_.load(std::memory_order_relaxed);
    bool monitor = monitor_.load(std::memory_order_relaxed);

    while (frames > 0) {
        int n = int(std::min(frames, kMaxBlock));
        float* out = m.processed.data();
        m.processor.process(input, out, n, s);
        // Petite marge de sécurité : on ne laisse jamais sortir un son saturé.
        for (int i = 0; i < n; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
        // Retour casque : la voix seule (les sons du soundboard ont leur propre lecteur sur le casque).
        if (monitor) {
            m.toMonitor.push(out, n);
        } else {
            float* silence = m.silence.data();
            m.toMonitor.push(silence, n);
        }
        // Soundboard : les sons s'ajoutent à la voix, après l'effet (l'autotune ne les touche pas).
        if (mixer_) {
            mixer_->mix(out, n);
            for (int i = 0; i < n; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
        }
        m.toCable.push(out, n);
        input += n;
        frames -= ma_uint32(n);
    }
    const auto& r = m.processor.readout();
    detected_.store(r.detectedMidi, std::memory_order_relaxed);
    target_.store(r.targetMidi, std::memory_order_relaxed);
    level_.store(r.level, std::memory_order_relaxed);
}

uint64_t VoiceEngine::underruns() const { return impl_->toCable.underruns(); }
