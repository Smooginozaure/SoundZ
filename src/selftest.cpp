// Auto-test : « SoundZ.exe --selftest rapport.txt »
// Joue un la 440 Hz sur le sas (la sortie Realtek, où rien n'est branché), le récupère en loopback,
// puis vérifie que la transposition donne la bonne fréquence. Ne touche pas à la sortie par défaut.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "default_output.h"
#include "engine.h"
#include "transposer.h"
#include "util.h"

namespace {

constexpr ma_uint32 kRate = PitchEngine::kSampleRate;
constexpr double kPi = 3.14159265358979323846;
constexpr double kToneHz = 440.0;
constexpr float kToneLevel = 0.25f;

struct ToneState {
    double freq = kToneHz;
    float level = kToneLevel;
    double phase = 0;
};

struct Recorder {
    std::vector<float> samples = std::vector<float>(size_t(kRate) * 4 * 2);
    std::atomic<size_t> frames{0};
};

void toneCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* tone = static_cast<ToneState*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    for (ma_uint32 i = 0; i < frames; ++i) {
        float v = tone->level * float(std::sin(tone->phase));
        tone->phase += 2 * kPi * tone->freq / kRate;
        if (tone->phase > 2 * kPi) tone->phase -= 2 * kPi;
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
}

void recordCallback(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* rec = static_cast<Recorder*>(device->pUserData);
    size_t start = rec->frames.load();
    size_t room = rec->samples.size() / 2 - start;
    size_t n = std::min<size_t>(frames, room);
    std::memcpy(rec->samples.data() + start * 2, input, n * 2 * sizeof(float));
    rec->frames.store(start + n);
}

// Joue le la sur le sas et le récupère en loopback. Renvoie le canal gauche (sans début ni fin).
std::vector<float> captureTone(const OutputDevice& sas, double freq = kToneHz, float level = kToneLevel) {
    std::vector<float> captured;
    ma_context context;
    ma_backend backends[] = {ma_backend_wasapi};
    if (ma_context_init(backends, 1, nullptr, &context) != MA_SUCCESS) return captured;

    ToneState tone;
    tone.freq = freq;
    tone.level = level;
    ma_device_config playConfig = ma_device_config_init(ma_device_type_playback);
    playConfig.playback.pDeviceID = &sas.id;
    playConfig.playback.format = ma_format_f32;
    playConfig.playback.channels = 2;
    playConfig.sampleRate = kRate;
    playConfig.dataCallback = toneCallback;
    playConfig.pUserData = &tone;

    Recorder recorder;
    ma_device_config recConfig = ma_device_config_init(ma_device_type_loopback);
    recConfig.capture.pDeviceID = &sas.id;
    recConfig.capture.format = ma_format_f32;
    recConfig.capture.channels = 2;
    recConfig.sampleRate = kRate;
    recConfig.dataCallback = recordCallback;
    recConfig.pUserData = &recorder;

    ma_device player, rec;
    if (ma_device_init(&context, &playConfig, &player) == MA_SUCCESS) {
        if (ma_device_init(&context, &recConfig, &rec) == MA_SUCCESS) {
            ma_device_start(&rec);
            ma_device_start(&player);
            Sleep(2500);
            ma_device_stop(&player);
            ma_device_stop(&rec);
            ma_device_uninit(&rec);
            size_t frames = recorder.frames.load();
            for (size_t i = kRate / 2; i + kRate / 4 < frames; ++i) captured.push_back(recorder.samples[2 * i]);
        }
        ma_device_uninit(&player);
    }
    ma_context_uninit(&context);
    return captured;
}

// Amplitude d'une fréquence (DFT avec fenêtre de Hann).
double magnitudeAt(const std::vector<float>& x, double freq) {
    double re = 0, im = 0;
    const double n = double(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / n);
        double phase = 2 * kPi * freq * double(i) / kRate;
        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
    }
    return std::sqrt(re * re + im * im);
}

// Fréquence dominante autour de `center` (±4 %), précision ~0,05 Hz.
double peakFrequency(const std::vector<float>& x, double center) {
    double best = 0, bestMag = -1;
    for (double step : {1.0, 0.05}) {
        double from = bestMag < 0 ? center * 0.96 : best - 1.0;
        double to = bestMag < 0 ? center * 1.04 : best + 1.0;
        for (double f = from; f <= to; f += step) {
            double m = magnitudeAt(x, f);
            if (m > bestMag) { bestMag = m; best = f; }
        }
    }
    return best;
}

double rms(const std::vector<float>& mono) {
    double sum = 0;
    for (float v : mono) sum += double(v) * v;
    return mono.empty() ? 0 : std::sqrt(sum / double(mono.size()));
}

std::vector<float> transpose(const std::vector<float>& mono, int semitones) {
    Transposer<1> transposer;
    transposer.configure(480);
    transposer.setSemitones(semitones);
    std::vector<float> out(mono.size());
    const int chunk = 480;
    for (size_t pos = 0; pos + chunk <= mono.size(); pos += chunk) {
        const float* in[1] = {mono.data() + pos};
        float* o[1] = {out.data() + pos};
        transposer.process(in, chunk, o, chunk);
    }
    // On jette le début (latence + mise en route).
    return std::vector<float>(out.begin() + kRate / 2, out.end() - chunk);
}

}  // namespace

int runSelfTest(const std::wstring& reportPath) {
    if (FindWindowW(L"SoundZWindow", nullptr) || FindWindowW(L"VocoderPitchWindow", nullptr)) {
        // SoundZ est ouvert : CABLE Input est alors le sas de la musique, le son de test sortirait dans le casque.
        const char text[] = "ECHEC : ferme SoundZ avant ce test (le son de test sortirait dans ton casque)\r\n";
        if (FILE* f = _wfopen(reportPath.c_str(), L"wb")) {
            fwrite(text, 1, sizeof(text) - 1, f);
            fclose(f);
        }
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string report;
    auto line = [&](const std::string& s) { report += s + "\r\n"; };
    char buf[512];
    int failures = 0;

    PitchEngine engine;
    std::wstring error;
    if (!engine.init(error)) {
        line("ECHEC init : " + narrow(error));
        failures++;
    }

    auto devices = engine.outputs();
    line("Sorties audio trouvées :");
    const OutputDevice* sas = nullptr;
    const OutputDevice* out = nullptr;
    for (const auto& d : devices) {
        line("  - " + narrow(d.name));
        if (!out && contains(d.name, L"Sound Blaster G8")) out = &d;
    }
    for (const char* wanted : {"CABLE Input"}) {
        for (const auto& d : devices) {
            if (!sas && &d != out && contains(d.name, widen(wanted))) sas = &d;
        }
    }
    line("Sortie choisie : " + (out ? narrow(out->name) : std::string("AUCUNE")));
    line("Sas choisi     : " + (sas ? narrow(sas->name) : std::string("AUCUN")));
    if (!out) failures++;

    const double expected = kToneLevel / std::sqrt(2.0);
    std::vector<float> captured;
    if (sas) {
        EndpointVolume sasVolume;
        float volume = 1;
        bool muted = false;
        bool haveVolume = sasVolume.open(sas->endpoint) && sasVolume.get(volume, muted);
        int effects = getEffectsDisabled(sas->endpoint);
        snprintf(buf, sizeof(buf), "Sas : volume %.0f %%%s, améliorations audio %s", volume * 100, muted ? " (muet)" : "",
                 effects == 1 ? "désactivées" : "actives");
        line(buf);

        // Mesure du gain selon le niveau et la fréquence : 0 dB partout = son transmis tel quel.
        auto measure = [&](const char* title) {
            line(title);
            int bad = 0;
            for (float level : {0.02f, 0.1f, 0.5f}) {
                auto test = captureTone(*sas, kToneHz, level);
                double gain = 20 * std::log10(std::max(rms(test), 1e-9) / (level / std::sqrt(2.0)));
                snprintf(buf, sizeof(buf), "  440 Hz à %+.0f dBFS : %+.1f dB", 20 * std::log10(level), gain);
                line(buf);
                if (std::fabs(gain) > 0.5) bad++;
            }
            for (double freq : {60.0, 1000.0, 8000.0, 15000.0}) {
                auto test = captureTone(*sas, freq, kToneLevel);
                double gain = 20 * std::log10(std::max(rms(test), 1e-9) / expected);
                snprintf(buf, sizeof(buf), "  %.0f Hz à -12 dBFS : %+.1f dB", freq, gain);
                line(buf);
                if (std::fabs(gain) > 0.5) bad++;
            }
            return bad;
        };

        measure("Son récupéré, réglages actuels du sas :");

        // Comme l'appli : effets coupés et volume à 100 % pendant le fonctionnement.
        if (effects != 1) {
            setEffectsDisabled(sas->endpoint, true);
            Sleep(200);
        }
        if (haveVolume) sasVolume.set(1.0f, false);
        if (measure("Son récupéré, réglages de l'appli (effets coupés, volume 100 %) :") > 0) failures++;
        captured = captureTone(*sas);

        if (effects != 1) setEffectsDisabled(sas->endpoint, false);
        if (haveVolume) sasVolume.set(volume, muted);
        line("Réglages du sas remis comme avant.");
    }

    if (captured.size() > size_t(kRate)) {
        double freq = peakFrequency(captured, kToneHz);
        snprintf(buf, sizeof(buf), "Son récupéré : %.2f Hz (attendu 440)", freq);
        line(buf);
        if (std::fabs(freq - kToneHz) > 0.5) failures++;

        for (int st : {-4, -3, -2, -1, 1, 2, 3, 4}) {
            auto shifted = transpose(captured, st);
            double target = kToneHz * std::pow(2.0, st / 12.0);
            double got = peakFrequency(shifted, target);
            double cents = 1200 * std::log2(got / target);
            bool good = std::fabs(cents) < 5;
            snprintf(buf, sizeof(buf), "Transposition %+d : %.2f Hz (attendu %.2f, écart %+.1f centièmes) %s", st, got,
                     target, cents, good ? "OK" : "ECHEC");
            line(buf);
            if (!good) failures++;
        }
    } else if (sas) {
        line("ECHEC : pas assez de son récupéré sur le sas");
        failures++;
    }

    {
        Transposer<2> t;
        t.configure(480);
        snprintf(buf, sizeof(buf), "Latence du traitement : %.0f ms (+ réserve 30 ms + tampons Windows)",
                 t.latencyFrames() * 1000.0 / kRate);
        line(buf);
    }

    line(failures == 0 ? "RESULTAT : OK" : "RESULTAT : " + std::to_string(failures) + " problème(s)");

    FILE* f = _wfopen(reportPath.c_str(), L"wb");
    if (f) {
        fwrite(report.data(), 1, report.size(), f);
        fclose(f);
    }
    CoUninitialize();
    return failures == 0 ? 0 : 1;
}

// Test du moteur en temps réel, sans rien jouer d'audible :
// la 440 Hz joué sur le sas (VB-CABLE) -> moteur -> une sortie muette choisie (3e argument), écoutée en loopback.
int runEngineTest(const std::wstring& reportPath, const std::wstring& testOutput) {
    if (FindWindowW(L"SoundZWindow", nullptr) || FindWindowW(L"VocoderPitchWindow", nullptr)) {
        // SoundZ est ouvert : CABLE Input est alors le sas de la musique, le son de test sortirait dans le casque.
        const char text[] = "ECHEC : ferme SoundZ avant ce test (le son de test sortirait dans ton casque)\r\n";
        if (FILE* f = _wfopen(reportPath.c_str(), L"wb")) {
            fwrite(text, 1, sizeof(text) - 1, f);
            fclose(f);
        }
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string report;
    auto line = [&](const std::string& s) { report += s + "\r\n"; };
    char buf[512];
    int failures = 0;

    PitchEngine engine;
    std::wstring error;
    engine.init(error);
    auto devices = engine.outputs();
    const OutputDevice* sas = nullptr;
    const OutputDevice* out = nullptr;
    for (const auto& d : devices) {
        if (!sas && contains(d.name, L"CABLE Input")) sas = &d;
        if (!out && !testOutput.empty() && contains(d.name, testOutput)) out = &d;
    }
    if (!sas || !out) {
        line("ECHEC : sorties de test introuvables");
        failures++;
    } else {
        line("Sas : " + narrow(sas->name) + "  ->  sortie de test : " + narrow(out->name));
        int effects = getEffectsDisabled(sas->endpoint);
        if (effects != 1) { setEffectsDisabled(sas->endpoint, true); Sleep(200); }

        ma_context context;
        ma_backend backends[] = {ma_backend_wasapi};
        ma_context_init(backends, 1, nullptr, &context);

        ToneState tone;
        ma_device_config playConfig = ma_device_config_init(ma_device_type_playback);
        playConfig.playback.pDeviceID = &sas->id;
        playConfig.playback.format = ma_format_f32;
        playConfig.playback.channels = 2;
        playConfig.sampleRate = kRate;
        playConfig.dataCallback = toneCallback;
        playConfig.pUserData = &tone;

        Recorder recorder;
        recorder.samples.assign(size_t(kRate) * 10 * 2, 0.0f);
        ma_device_config recConfig = ma_device_config_init(ma_device_type_loopback);
        recConfig.capture.pDeviceID = &out->id;
        recConfig.capture.format = ma_format_f32;
        recConfig.capture.channels = 2;
        recConfig.sampleRate = kRate;
        recConfig.dataCallback = recordCallback;
        recConfig.pUserData = &recorder;

        ma_device player, rec;
        ma_device_init(&context, &playConfig, &player);
        ma_device_init(&context, &recConfig, &rec);
        ma_device_start(&player);
        engine.setSemitones(2);
        if (!engine.start(*sas, *out, error)) {
            line("ECHEC démarrage moteur : " + narrow(error));
            failures++;
        }
        ma_device_start(&rec);
        Sleep(4000);
        engine.setSemitones(-3);
        Sleep(4000);
        ma_device_stop(&rec);
        engine.stop();
        ma_device_stop(&player);
        ma_device_uninit(&rec);
        ma_device_uninit(&player);
        ma_context_uninit(&context);
        if (effects != 1) setEffectsDisabled(sas->endpoint, false);

        size_t frames = recorder.frames.load();
        snprintf(buf, sizeof(buf), "Reçu sur la sortie de test : %.2f s, coupures côté moteur : %llu", double(frames) / kRate,
                 (unsigned long long)engine.underruns());
        line(buf);

        struct Segment { double from, to; int semitones; };
        for (Segment s : {Segment{1.0, 3.8, 2}, Segment{4.6, 7.8, -3}}) {
            std::vector<float> seg;
            for (size_t i = size_t(s.from * kRate); i < size_t(s.to * kRate) && i < frames; ++i) seg.push_back(recorder.samples[2 * i]);
            if (seg.size() < kRate) { line("ECHEC : segment trop court"); failures++; continue; }
            double target = kToneHz * std::pow(2.0, s.semitones / 12.0);
            double got = peakFrequency(seg, target);
            double cents = 1200 * std::log2(got / target);
            // Coupures audibles : fenêtres de 5 ms nettement plus faibles que la moyenne.
            const size_t win = kRate / 200;
            std::vector<double> levels;
            for (size_t p = 0; p + win <= seg.size(); p += win) levels.push_back(rms(std::vector<float>(seg.begin() + p, seg.begin() + p + win)));
            double mean = 0;
            for (double l : levels) mean += l;
            mean /= double(levels.size());
            int dips = 0;
            for (double l : levels) if (l < mean * 0.7) dips++;
            double level = 20 * std::log10(std::max(mean, 1e-9) / (kToneLevel / std::sqrt(2.0)));
            bool good = std::fabs(cents) < 2 && dips == 0;
            snprintf(buf, sizeof(buf), "%+d demi-tons : %.2f Hz (attendu %.2f, %+.1f centièmes), niveau %+.1f dB, creux : %d  %s",
                     s.semitones, got, target, cents, level, dips, good ? "OK" : "ECHEC");
            line(buf);
            if (!good) failures++;
        }
    }
    line(failures == 0 ? "RESULTAT : OK" : "RESULTAT : " + std::to_string(failures) + " problème(s)");
    FILE* f = _wfopen(reportPath.c_str(), L"wb");
    if (f) { fwrite(report.data(), 1, report.size(), f); fclose(f); }
    CoUninitialize();
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------- test de la voix, sans rien d'audible
// Une « voix » de synthèse chantée un peu faux (225 Hz) est jouée dans CABLE-B (faux micro) ;
// le moteur voix applique l'effet et l'envoie dans CABLE-A ; on écoute CABLE-A Output (ce qu'entend Discord).
#include "voice_engine.h"

namespace {

struct VoiceSource {
    double phase = 0;
    float a1 = 0, b1 = 0, a2 = 0, b2 = 0;
    float next() {
        auto res = [](float x, double f, double bw, float& a, float& b) {
            double r = std::exp(-kPi * bw / kRate), c = 2 * r * std::cos(2 * kPi * f / kRate);
            float o = float(x * (1 - r) + c * a - r * r * b);
            b = a;
            a = o;
            return o;
        };
        phase += 225.0 / kRate;
        if (phase >= 1) phase -= 1;
        float saw = float(2 * phase - 1);
        return 0.5f * (res(saw, 700, 110, a1, b1) + 0.6f * res(saw, 1200, 120, a2, b2));
    }
};

void voiceSourceCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* src = static_cast<VoiceSource*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    for (ma_uint32 i = 0; i < frames; ++i) out[i] = src->next();
}

void monoRecordCallback(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* rec = static_cast<Recorder*>(device->pUserData);
    size_t start = rec->frames.load();
    size_t n = std::min<size_t>(frames, rec->samples.size() - start);
    std::memcpy(rec->samples.data() + start, input, n * sizeof(float));
    rec->frames.store(start + n);
}

}  // namespace

int runVoiceTest(const std::wstring& reportPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string report;
    auto line = [&](const std::string& s) { report += s + "\r\n"; };
    char buf[512];
    int failures = 0;

    VoiceEngine engine;
    std::wstring error;
    engine.init(error);
    auto mics = engine.microphones();
    auto outs = engine.outputs();
    const OutputDevice *fakeMic = nullptr, *fakeMicInput = nullptr, *cableA = nullptr, *cableAOut = nullptr;
    for (const auto& d : mics) {
        if (!fakeMic && contains(d.name, L"CABLE-B Output")) fakeMic = &d;
        if (!cableAOut && contains(d.name, L"CABLE-A Output")) cableAOut = &d;
    }
    for (const auto& d : outs) {
        if (!fakeMicInput && contains(d.name, L"CABLE-B Input")) fakeMicInput = &d;
        if (!cableA && contains(d.name, L"CABLE-A Input")) cableA = &d;
    }
    if ((FindWindowW(L"SoundZWindow", nullptr) || FindWindowW(L"VocoderPitchWindow", nullptr))) {
        // SoundZ est ouvert : CABLE-A est alors le micro de Discord. Pas de voix de test dedans !
        line("ECHEC : ferme SoundZ avant ce test (CABLE-A sert de micro à Discord)");
        failures++;
    } else if (!fakeMic || !fakeMicInput || !cableA || !cableAOut) {
        line("ECHEC : CABLE-A / CABLE-B introuvables");
        failures++;
    } else {
        ma_context context;
        ma_backend backends[] = {ma_backend_wasapi};
        ma_context_init(backends, 1, nullptr, &context);

        VoiceSource source;
        ma_device_config playConfig = ma_device_config_init(ma_device_type_playback);
        playConfig.playback.pDeviceID = &fakeMicInput->id;
        playConfig.playback.format = ma_format_f32;
        playConfig.playback.channels = 1;
        playConfig.sampleRate = kRate;
        playConfig.dataCallback = voiceSourceCallback;
        playConfig.periodSizeInMilliseconds = 30;  // outil de test : grosse marge, il ne doit jamais couper
        playConfig.pUserData = &source;
        ma_device player;
        ma_device_init(&context, &playConfig, &player);
        ma_device_start(&player);

        struct Case { const char* name; int mode; int transpose; bool chords; double expect; };
        for (Case c : {Case{"Sans effet", voice::kModeNone, 0, false, 225.0},
                       Case{"Autotune Do majeur (225 -> La 220)", voice::kModeAutotune, 0, false, 220.0},
                       Case{"Autotune +7 demi-tons (-> Mi 329,6)", voice::kModeAutotune, 7, false, 329.63},
                       Case{"Vocodeur (-> La 220)", voice::kModeVocoder, 0, false, 220.0}}) {
            engine.setMode(c.mode);
            engine.setKey(0);
            engine.setScale(0);
            engine.setTranspose(c.transpose);
            engine.setRetuneMs(0);
            engine.setAmount(1);
            engine.setGateDb(-80);
            engine.setChords(c.chords);
            engine.setMonitor(false);
            if (!engine.start(*fakeMic, *cableA, nullptr, error)) {
                line("ECHEC démarrage voix : " + narrow(error));
                failures++;
                break;
            }
            Recorder rec;
            rec.samples.assign(size_t(kRate) * 4, 0.0f);
            ma_device_config recConfig = ma_device_config_init(ma_device_type_capture);
            recConfig.capture.pDeviceID = &cableAOut->id;
            recConfig.capture.format = ma_format_f32;
            recConfig.capture.channels = 1;
            recConfig.sampleRate = kRate;
            recConfig.dataCallback = monoRecordCallback;
            recConfig.periodSizeInMilliseconds = 30;
            recConfig.pUserData = &rec;
            ma_device recorder;
            ma_device_init(&context, &recConfig, &recorder);
            ma_device_start(&recorder);
            Sleep(3000);
            ma_device_uninit(&recorder);
            uint64_t underrunsSeen = engine.underruns();
            engine.stop();

            size_t frames = rec.frames.load();
            std::vector<float> seg;
            for (size_t i = kRate; i < frames; ++i) seg.push_back(rec.samples[i]);
            // Coupures : fenêtres de 5 ms nettement plus faibles que la moyenne.
            const size_t win = kRate / 200;
            std::vector<double> levels;
            for (size_t p = 0; p + win <= seg.size(); p += win)
                levels.push_back(rms(std::vector<float>(seg.begin() + p, seg.begin() + p + win)));
            double mean = 0;
            for (double l : levels) mean += l;
            mean /= double(std::max<size_t>(1, levels.size()));
            int dips = 0;
            for (double l : levels) if (l < mean * 0.5) dips++;
            double got = seg.size() > kRate ? peakFrequency(seg, c.expect) : 0;
            double cents = got > 0 ? 1200 * std::log2(got / c.expect) : 999;
            bool good = std::fabs(cents) < 3 && dips == 0 && mean > 0.01;
            snprintf(buf, sizeof(buf), "%-38s : %.2f Hz (attendu %.2f, %+.1f c), niveau %.3f, creux %d (manques moteur %llu)  %s", c.name, got,
                     c.expect, cents, mean, dips, (unsigned long long)underrunsSeen, good ? "OK" : "ECHEC");
            line(buf);
            if (!good) failures++;
        }
        ma_device_uninit(&player);
        ma_context_uninit(&context);
    }
    line(failures == 0 ? "RESULTAT : OK" : "RESULTAT : " + std::to_string(failures) + " problème(s)");
    FILE* f = _wfopen(reportPath.c_str(), L"wb");
    if (f) { fwrite(report.data(), 1, report.size(), f); fclose(f); }
    CoUninitialize();
    return failures == 0 ? 0 : 1;
}

#include "soundtest.inc"
