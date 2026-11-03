// ---------------------------------------------------------------- soundboard (inclus à la fin de selftest.cpp)
// « SoundZ.exe --soundtest rapport.txt » : nettoyage d'un son bruité, images, fichiers, puis un son
// envoyé par le mélangeur dans CABLE-A (micro virtuel) et réenregistré sur CABLE-A Output. Rien d'audible.
#include "sound_dsp.h"
#include "sound_image.h"
#include "sound_mixer.h"
#include "sound_replay.h"

// Niveau moyen (dB) d'une bande de fréquences.
double bandDb(const std::vector<float>& x, double lo, double hi) {
    const int N = 4096;
    double e = 0;
    int frames = 0;
    for (size_t at = 0; at + N <= x.size(); at += N / 2, ++frames) {
        std::vector<std::complex<float>> s(N);
        for (int i = 0; i < N; ++i) s[size_t(i)] = x[at + size_t(i)] * float(0.5 - 0.5 * std::cos(2 * kPi * i / N));
        sound::fft(s, false);
        for (int k = int(lo * N / kRate); k < int(hi * N / kRate); ++k) e += std::norm(s[size_t(k)]);
    }
    return 10 * std::log10(e / std::max(1, frames) + 1e-20);
}

int runSoundTest(const std::wstring& reportPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string report;
    auto line = [&](const std::string& s) { report += s + "\r\n"; };
    char buf[512];
    int failures = 0;
    auto check = [&](bool good, const char* text) {
        snprintf(buf, sizeof(buf), "%s  %s", text, good ? "OK" : "ECHEC");
        line(buf);
        if (!good) failures++;
    };
    auto rmsRange = [](const std::vector<float>& x, double from, double to) {
        std::vector<float> part(x.begin() + long(from * kRate), x.begin() + long(to * kRate));
        return rms(part);
    };
    auto db = [](double v) { return 20 * std::log10(std::max(v, 1e-12)); };
    char text[512];

    // 1. Un « son » (voyelle à 180 Hz, de 0,8 à 2,0 s) noyé dans un souffle à −40 dB.
    const size_t n = size_t(kRate) * 3;
    std::vector<float> clean(n, 0.0f), noisy(n);
    uint32_t seed = 12345;
    auto noise = [&]() {  // bruit blanc à peu près gaussien
        double s = 0;
        for (int k = 0; k < 4; ++k) {
            seed = seed * 1664525u + 1013904223u;
            s += double(seed >> 8) / double(1 << 24) - 0.5;
        }
        return float(s * 0.0173);  // ≈ 0,01 efficace
    };
    for (size_t i = 0; i < n; ++i) {
        double t = double(i) / kRate;
        if (t >= 0.8 && t < 2.0) {
            double env = std::min({1.0, (t - 0.8) / 0.02, (2.0 - t) / 0.05});
            double v = 0;
            for (int k = 1; k <= 8; ++k) v += std::sin(2 * kPi * 180.0 * k * t) / k;
            clean[i] = float(0.12 * env * v);
        }
        noisy[i] = clean[i] + noise();
    }
    // Le filtre anti-graves décale un peu la phase : la référence passe par le même filtre.
    sound::highPass(clean);
    std::vector<float> filtered = noisy;
    sound::highPass(filtered);
    std::vector<float> denoised = filtered;
    sound::denoise(denoised, 1.0f);
    double before = rmsRange(noisy, 0.1, 0.6), after = rmsRange(denoised, 0.1, 0.6);
    snprintf(text, sizeof(text), "Souffle seul : %.1f dB -> %.1f dB (réduction %.1f dB, attendu >= 12)", db(before),
             db(after), db(before) - db(after));
    check(db(before) - db(after) >= 12, text);
    double errBefore = 0, errAfter = 0, sig = 0;
    for (size_t i = size_t(1.0 * kRate); i < size_t(1.8 * kRate); ++i) {
        sig += double(clean[i]) * clean[i];
        errBefore += double(filtered[i] - clean[i]) * (filtered[i] - clean[i]);
        errAfter += double(denoised[i] - clean[i]) * (denoised[i] - clean[i]);
    }
    double snrBefore = 10 * std::log10(sig / errBefore), snrAfter = 10 * std::log10(sig / errAfter);
    snprintf(text, sizeof(text), "Pendant le son : rapport signal/bruit %.1f dB -> %.1f dB (attendu : meilleur)", snrBefore,
             snrAfter);
    check(snrAfter > snrBefore + 3, text);

    sound::CleanResult r = sound::clean(noisy, 1.0f);
    double length = double(r.samples.size()) / kRate;
    float peak = 0;
    for (float s : r.samples) peak = std::max(peak, std::fabs(s));
    snprintf(text, sizeof(text),
             "Nettoyage complet : durée %.2f s (attendu 1,2 à 1,45 : silences coupés), crête %.1f dB (<= -1)", length,
             db(peak));
    check(!r.empty && length > 1.2 && length < 1.45 && peak <= 0.892f, text);
    check(sound::clean(std::vector<float>(n, 0.0f), 0.6f).empty, "Capture silencieuse reconnue comme vide");

    // Musique continue, sans pause (basse, accords tenus, charleston) : le nettoyage ne doit rien abîmer.
    {
        std::vector<float> music(n);
        const double bassNotes[] = {55, 73.4, 65.4, 82.4};
        double bassPhase = 0;
        for (size_t i = 0; i < n; ++i) {
            double t = double(i) / kRate;
            bassPhase += bassNotes[int(t * 2) % 4] / kRate;
            double bass = 0;
            for (int k = 1; k <= 6; ++k) bass += std::sin(2 * kPi * k * bassPhase) / (k * k);
            double pad = 0;
            for (double hz : {261.6, 329.6, 392.0, 523.3, 659.3}) pad += std::sin(2 * kPi * hz * t) * 0.3;
            double hat = std::fmod(t, 0.125) < 0.03 ? noise() * 8 * (1 - std::fmod(t, 0.125) / 0.03) : 0;
            music[i] = float(0.25 * bass + 0.08 * pad + hat);
        }
        sound::CleanResult a = sound::clean(music, 0.0f), b = sound::clean(music, 0.6f);
        // Seule la forme du spectre compte (un écart identique partout n'est qu'une question de volume).
        std::vector<double> diffs;
        for (double lo : {40.0, 80.0, 160.0, 320.0, 640.0, 1280.0, 2560.0, 5120.0})
            diffs.push_back(bandDb(b.samples, lo, lo * 2) - bandDb(a.samples, lo, lo * 2));
        double mean = 0;
        for (double d : diffs) mean += d / double(diffs.size());
        double worst = 0;
        std::string bands;
        for (double d : diffs) {
            worst = std::max(worst, std::fabs(d - mean));
            snprintf(text, sizeof(text), " %+.1f", d - mean);
            bands += text;
        }
        snprintf(text, sizeof(text), "Musique continue : écart par octave (40 Hz -> 10 kHz)%s dB (attendu : moins de 1,5 dB)", bands.c_str());
        check(worst < 1.5, text);
    }

    // 2. Fichier WAV : aller-retour.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring wav = std::wstring(temp) + L"vocoder_test.wav", png = std::wstring(temp) + L"vocoder_test.png";
    std::vector<float> back;
    bool wavOk = sound::writeWav(wav, r.samples) && sound::readWav(wav, back) && back.size() == r.samples.size();
    float maxDiff = 0;
    for (size_t i = 0; wavOk && i < back.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(back[i] - r.samples[i]));
    snprintf(text, sizeof(text), "WAV écrit puis relu (écart max %.6f)", maxDiff);
    check(wavOk && maxDiff < 1.0f / 16000, text);
    DeleteFileW(wav.c_str());

    // 3. Images : emoji en couleur, texte, PNG aller-retour.
    soundimg::init();
    soundimg::Image emoji, word, loaded;
    bool emojiOk = soundimg::renderText(L"💨", emoji);
    int opaque = 0, colored = 0;
    for (uint32_t px : emoji.pixels) {
        int a = int(px >> 24), rr = int((px >> 16) & 255), g = int((px >> 8) & 255), b = int(px & 255);
        if (a > 128) {
            opaque++;
            if (std::abs(rr - g) > 30 || std::abs(g - b) > 30 || std::abs(rr - b) > 30) colored++;
        }
    }
    snprintf(text, sizeof(text), "Emoji dessiné : %d pixels visibles, dont %d en couleur", opaque, colored);
    check(emojiOk && opaque > 1500 && colored > 300, text);
    check(soundimg::renderText(L"LOL", word) && !word.empty(), "Texte « LOL » dessiné");
    bool pngOk = soundimg::savePng(png, emoji) && soundimg::loadFile(png, loaded) &&
                 loaded.pixels.size() == emoji.pixels.size();
    double diff = 0;
    for (size_t i = 0; pngOk && i < loaded.pixels.size(); ++i)
        for (int c = 0; c < 32; c += 8)
            diff += std::abs(int((loaded.pixels[i] >> c) & 255) - int((emoji.pixels[i] >> c) & 255));
    if (pngOk) diff /= double(loaded.pixels.size() * 4);
    snprintf(text, sizeof(text), "PNG enregistré puis relu (écart moyen %.2f)", diff);
    check(pngOk && diff < 2, text);
    DeleteFileW(png.c_str());
    soundimg::Image drawing;
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\"><defs><radialGradient id=\"g\" cx=\"40%\" "
        "cy=\"35%\" r=\"70%\"><stop offset=\"0\" stop-color=\"#C8F27A\"/><stop offset=\"1\" stop-color=\"#7FCB3A\"/>"
        "</radialGradient></defs><circle cx=\"64\" cy=\"64\" r=\"50\" fill=\"url(#g)\" stroke=\"#2B2B2B\" "
        "stroke-width=\"6\"/><path d=\"M40 70 Q64 95 88 70\" fill=\"none\" stroke=\"#2B2B2B\" stroke-width=\"6\" "
        "stroke-linecap=\"round\"/></svg>";
    int green = 0;
    bool svgOk = soundimg::renderSvg(svg, drawing);
    for (uint32_t px : drawing.pixels)
        if ((px >> 24) > 200 && ((px >> 8) & 255) > ((px >> 16) & 255) + 30) green++;
    snprintf(text, sizeof(text), "Dessin SVG affiché : %d pixels verts (attendu > 4000)", green);
    check(svgOk && green > 4000, text);
    check(!soundimg::renderSvg("ceci n'est pas un dessin", drawing), "Réponse qui n'est pas un dessin refusée");
    HBITMAP bitmap = soundimg::makeBitmap(emoji, 44);
    check(bitmap != nullptr, "Image mise à la taille d'un bouton");
    if (bitmap) DeleteObject(bitmap);
    soundimg::shutdown();

    // 4. Le son part dans le micro virtuel : mélangeur -> moteur de la voix -> CABLE-A -> CABLE-A Output.
    VoiceEngine engine;
    std::wstring error;
    engine.init(error);
    auto mics = engine.microphones();
    auto outs = engine.outputs();
    const OutputDevice *fakeMic = nullptr, *cableA = nullptr, *cableAOut = nullptr;
    for (const auto& d : mics) {
        if (!fakeMic && contains(d.name, L"CABLE-B Output")) fakeMic = &d;
        if (!cableAOut && contains(d.name, L"CABLE-A Output")) cableAOut = &d;
    }
    for (const auto& d : outs)
        if (!cableA && contains(d.name, L"CABLE-A Input")) cableA = &d;
    if ((FindWindowW(L"SoundZWindow", nullptr) || FindWindowW(L"VocoderPitchWindow", nullptr))) {
        // SoundZ est ouvert : CABLE-A est alors le micro de Discord. Pas de son de test dedans !
        line("Son dans CABLE-A : ignoré (SoundZ est ouvert ; ferme-le pour faire ce test)");
    } else if (!fakeMic || !cableA || !cableAOut) {
        check(false, "CABLE-A / CABLE-B introuvables");
    } else {
        SoundMixer mixer;
        mixer.setVolume(0.64f);
        std::vector<float> tone(static_cast<size_t>(kRate));  // 1 s de la 440 Hz à 0,5
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * float(std::sin(2 * kPi * 440.0 * double(i) / kRate));
        engine.setSoundMixer(&mixer);
        engine.setMode(voice::kModeNone);
        engine.setGateDb(-20);  // le faux micro est muet : la voix ne doit rien ajouter
        engine.setMonitor(false);
        if (!engine.start(*fakeMic, *cableA, nullptr, error)) {
            check(false, ("Démarrage de la voix : " + narrow(error)).c_str());
        } else {
            ma_context context;
            ma_backend backends[] = {ma_backend_wasapi};
            ma_context_init(backends, 1, nullptr, &context);
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
            Sleep(800);
            mixer.play(tone.data(), int(tone.size()), 1.0f, 1);
            Sleep(300);
            uint64_t playingMid = mixer.playing();
            Sleep(1700);
            ma_device_uninit(&recorder);
            engine.stop();
            ma_context_uninit(&context);

            size_t frames = rec.frames.load();
            std::vector<float> got(rec.samples.begin(), rec.samples.begin() + long(frames));
            const size_t win = kRate / 100;
            int loud = 0;
            for (size_t p = 0; p + win <= got.size(); p += win)
                if (rms(std::vector<float>(got.begin() + long(p), got.begin() + long(p + win))) > 0.05) loud++;
            // Le passage sonore, d'un seul tenant (sans ses 20 premières et dernières ms).
            size_t from = got.size(), to = 0;
            for (size_t i = 0; i < got.size(); ++i)
                if (std::fabs(got[i]) > 0.1f) { from = std::min(from, i); to = i; }
            std::vector<float> loudPart;
            if (to > from + size_t(kRate / 25)) loudPart.assign(got.begin() + long(from + kRate / 50), got.begin() + long(to - kRate / 50));
            double level = loudPart.empty() ? 0 : rms(loudPart);
            double expect = 0.5 * 0.64 / std::sqrt(2.0);
            double freq = loudPart.size() > size_t(kRate / 2) ? peakFrequency(loudPart, 440.0) : 0;
            snprintf(text, sizeof(text),
                     "Son dans CABLE-A : %.1f Hz (attendu 440), niveau %.3f (attendu %.3f), durée %.2f s (attendu 1), "
                     "affiché en cours : %s",
                     freq, level, expect, loud / 100.0, (playingMid & 2) ? "oui" : "non");
            check(std::fabs(freq - 440) < 1 && std::fabs(level / expect - 1) < 0.05 && std::abs(loud - 100) <= 3 &&
                      (playingMid & 2),
                  text);
        }
    }

    // 5. Replay : les 10 dernières secondes d'une sortie (CABLE-B Input, inaudible), silences compris.
    {
        ma_context context;
        ma_backend backends[] = {ma_backend_wasapi};
        ma_context_init(backends, 1, nullptr, &context);
        const OutputDevice* cableB = nullptr;
        for (const auto& d : outs)
            if (!cableB && contains(d.name, L"CABLE-B Input")) cableB = &d;
        // Ce test joue un la de 1 s dans CABLE-B Input (le flux s'arrête ensuite : Windows n'envoie plus rien),
        // attend 1,5 s, puis regarde les « 10 dernières secondes ».
        enum Mode { kDevice, kOnlyMe, kAllButMe };
        auto capture = [&](Mode mode, bool& opened) {
            ReplayBuffer replay;
            opened = mode == kDevice ? replay.openDevice(&context, &cableB->id)
                                     : replay.openProcess(GetCurrentProcessId(), mode == kAllButMe);
            if (!opened) return std::vector<float>();
            Sleep(300);
            ToneState tone;
            ma_device_config c = ma_device_config_init(ma_device_type_playback);
            c.playback.pDeviceID = &cableB->id;
            c.playback.format = ma_format_f32;
            c.playback.channels = 2;
            c.sampleRate = kRate;
            c.dataCallback = toneCallback;
            c.pUserData = &tone;
            ma_device player;
            ma_device_init(&context, &c, &player);
            ma_device_start(&player);
            Sleep(1000);
            ma_device_uninit(&player);
            Sleep(1500);
            std::vector<float> got = replay.last(10);
            replay.close();
            return got;
        };
        auto describe = [&](const char* name, const std::vector<float>& got, double& length, double& endsBefore) {
            size_t from = got.size(), to = 0;
            for (size_t i = 0; i < got.size(); ++i)
                if (std::fabs(got[i]) > 0.05f) { from = std::min(from, i); to = i; }
            length = to > from ? double(to - from) / kRate : 0;
            endsBefore = to > from ? double(got.size() - to) / kRate : 0;
            snprintf(text, sizeof(text), "%s : %.2f s gardées, la de %.2f s (attendu 1), fini %.2f s avant la fin (attendu 1,5)",
                     name, double(got.size()) / kRate, length, endsBefore);
        };
        if (!cableB) {
            check(false, "Replay : CABLE-B Input introuvable");
        } else {
            bool opened = false;
            double length = 0, endsBefore = 0;
            std::vector<float> got = capture(kDevice, opened);
            // Mode de secours (Windows trop ancien pour la capture par programme) : seulement indicatif, VB-CABLE
            // décale parfois les premières dixièmes de seconde d'un flux qui démarre.
            describe("Replay d'une sortie (secours, indicatif)", got, length, endsBefore);
            line(text);
            check(opened, "Replay d'une sortie : écoute ouverte");

            got = capture(kOnlyMe, opened);
            describe("Replay par programme (SoundZ seul)", got, length, endsBefore);
            check(opened && std::fabs(length - 1) < 0.1 && std::fabs(endsBefore - 1.5) < 0.25, text);
            double mine = got.empty() ? 0 : magnitudeAt(got, kToneHz);

            // Tous les programmes sauf SoundZ : son propre la ne doit pas y être (d'autres sons du PC, si).
            got = capture(kAllButMe, opened);
            double others = got.empty() ? 0 : magnitudeAt(got, kToneHz);
            double db = 20 * std::log10(std::max(others, 1e-9) / std::max(mine, 1e-9));
            snprintf(text, sizeof(text), "Replay « tous sauf SoundZ » : le la de SoundZ est %.0f dB plus bas (attendu : absent, < -30 dB)",
                     db);
            check(opened && db < -30, text);
        }
        ma_context_uninit(&context);
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

// « SoundZ.exe --claudetest rapport.txt "nom du son" » : demande pour de vrai un emoji puis un dessin à
// Claude Code (utilise un peu du quota de l'abonnement), et enregistre le dessin à côté du rapport (.png).
#include "sound_claude.h"

int runClaudeTest(const std::wstring& reportPath, const std::wstring& name) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string report;
    auto line = [&](const std::string& s) { report += s + "\r\n"; };
    int failures = 0;
    soundimg::init();
    claude::check();
    for (int i = 0; i < 600 && claude::status() == claude::Status::kChecking; ++i) Sleep(100);
    const char* states[] = {"vérification…", "prêt", "introuvable", "pas connecté"};
    line(std::string("Claude Code : ") + states[int(claude::status())]);
    auto wait = [&](claude::Kind kind, claude::Result& r, double& seconds) {
        DWORD start = GetTickCount();
        if (!claude::request(kind, name)) return false;
        while (!claude::takeResult(r)) Sleep(100);
        seconds = (GetTickCount() - start) / 1000.0;
        return true;
    };
    if (claude::status() == claude::Status::kReady) {
        claude::Result r;
        double s = 0;
        bool ok = wait(claude::Kind::kEmoji, r, s) && r.ok;
        line("Emoji (" + std::to_string(int(s)) + " s) : " + (ok ? narrow(r.emoji) : "ECHEC " + narrow(r.error)));
        if (!ok) failures++;
        soundimg::Image image;
        ok = wait(claude::Kind::kDrawing, r, s) && r.ok && soundimg::renderSvg(r.svg, image);
        line("Dessin (" + std::to_string(int(s)) + " s, " + std::to_string(r.svg.size()) + " caractères) : " +
             (ok ? "affiché" : "ECHEC " + narrow(r.error)));
        if (ok) soundimg::savePng(reportPath + L".png", image);
        else failures++;
    } else {
        failures++;
    }
    claude::shutdown();
    soundimg::shutdown();
    line(failures == 0 ? "RESULTAT : OK" : "RESULTAT : " + std::to_string(failures) + " problème(s)");
    FILE* f = _wfopen(reportPath.c_str(), L"wb");
    if (f) {
        fwrite(report.data(), 1, report.size(), f);
        fclose(f);
    }
    CoUninitialize();
    return failures == 0 ? 0 : 1;
}

// « SoundZ.exe --shelltest rapport.txt » : active puis désactive le raccourci du menu Démarrer, l'icône du
// Bureau et le lancement avec Windows, en vérifiant chaque fois ; remet ensuite tout comme avant.
#include <shlobj.h>
#include "shell_integration.h"

int runShellTest(const std::wstring& reportPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string report;
    int failures = 0;
    auto line = [&](const std::string& s) { report += s + "\r\n"; };
    const struct { shellint::Item item; const char* name; } items[] = {
        {shellint::Item::kStartMenu, "Raccourci du menu Démarrer"},
        {shellint::Item::kDesktop, "Icône du Bureau"},
        {shellint::Item::kAutostart, "Lancement avec Windows"},
    };
    // On garde une copie exacte des raccourcis existants (et de l'ancienne clé « Run ») pour les remettre tels quels :
    // les recréer viserait l'exe du test, pas celui de l'utilisateur.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    wchar_t runValue[1024] = {};
    DWORD runSize = sizeof(runValue);
    const bool hadRun = RegGetValueW(HKEY_CURRENT_USER, runKey, L"SoundZ", RRF_RT_REG_SZ, nullptr, runValue, &runSize) == ERROR_SUCCESS;
    for (const auto& it : items) {
        const std::wstring file = shellint::shortcutFile(it.item);
        const std::wstring backup = std::wstring(temp) + L"soundz-shelltest-" + std::to_wstring(int(it.item)) + L".lnk";
        const bool hadFile = GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (hadFile) CopyFileW(file.c_str(), backup.c_str(), FALSE);
        const bool before = shellint::enabled(it.item);
        bool on = shellint::set(it.item, true) && shellint::enabled(it.item);
        bool off = shellint::set(it.item, false) && !shellint::enabled(it.item);
        if (hadFile) {  // comme avant le test, à l'octet près
            CopyFileW(backup.c_str(), file.c_str(), FALSE);
            DeleteFileW(backup.c_str());
        }
        if (it.item == shellint::Item::kAutostart && hadRun) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
                RegSetValueExW(key, L"SoundZ", 0, REG_SZ, reinterpret_cast<const BYTE*>(runValue), DWORD((wcslen(runValue) + 1) * sizeof(wchar_t)));
                RegCloseKey(key);
            }
        }
        bool restored = shellint::enabled(it.item) == before;
        line(std::string(it.name) + " : activer " + (on ? "OK" : "ECHEC") + ", désactiver " + (off ? "OK" : "ECHEC") +
             ", état d'origine (" + (before ? "actif" : "inactif") + ") remis " + (restored ? "OK" : "ECHEC"));
        if (!on || !off || !restored) failures++;
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

// « SoundZ.exe --cabletest rapport.txt » : répartition des câbles virtuels entre musique et voix, pour toutes les
// installations possibles (listes d'appareils simulées : rien n'est ouvert).
#include "cables.h"

int runCableTest(const std::wstring& reportPath) {
    struct Case {
        const char* name;
        std::vector<std::wstring> outputs;
        bool singleForMusic;
        const wchar_t* music;  // attendu (début du nom), ou nullptr
        const wchar_t* voice;
    };
    const std::wstring g8 = L"Haut-parleurs (Sound Blaster G8 USB-1)";
    const std::wstring vb = L"CABLE Input (VB-Audio Virtual Cable)", a = L"CABLE-A Input (VB-Audio Virtual Cable A)",
                       b = L"CABLE-B Input (VB-Audio Virtual Cable B)", hifi = L"Hi-Fi Cable Input (VB-Audio Hi-Fi Cable)",
                       vm = L"Voicemeeter Input (VB-Audio Voicemeeter VAIO)";
    const Case cases[] = {
        {"VB-CABLE + A+B", {g8, vb, a, b}, false, L"CABLE Input", L"CABLE-A Input"},
        {"VB-CABLE seul (gratuit)", {g8, vb}, false, nullptr, L"CABLE Input"},
        {"VB-CABLE seul, donné à la musique", {g8, vb}, true, L"CABLE Input", nullptr},
        {"VB-CABLE + Hi-Fi Cable (gratuits)", {g8, hifi, vb}, false, L"Hi-Fi Cable Input", L"CABLE Input"},
        {"Hi-Fi Cable seul", {g8, hifi}, false, nullptr, L"Hi-Fi Cable Input"},
        {"A+B seul", {g8, a, b}, false, L"CABLE-B Input", L"CABLE-A Input"},
        {"VB-CABLE + Voicemeeter", {g8, vb, vm}, false, nullptr, L"CABLE Input"},
        {"Voicemeeter seul", {g8, vm}, false, nullptr, L"Voicemeeter Input"},
        {"Aucun câble", {g8}, false, nullptr, nullptr},
    };
    std::string report;
    int failures = 0;
    for (const Case& c : cases) {
        cables::Assignment r = cables::assign(c.outputs, -1, -1, c.singleForMusic);
        auto got = [&](int i) { return i >= 0 ? c.outputs[size_t(i)] : std::wstring(L"(aucun)"); };
        auto ok = [&](int i, const wchar_t* want) { return want ? i >= 0 && cables::startsWith(c.outputs[size_t(i)], want) : i < 0; };
        bool good = ok(r.music, c.music) && ok(r.voice, c.voice);
        report += std::string(c.name) + " : musique = " + narrow(got(r.music)) + ", voix = " + narrow(got(r.voice)) + "  " +
                  (good ? "OK" : "ECHEC") + "\r\n";
        if (!good) failures++;
    }
    report += failures == 0 ? "RESULTAT : OK\r\n" : "RESULTAT : " + std::to_string(failures) + " problème(s)\r\n";
    if (FILE* f = _wfopen(reportPath.c_str(), L"wb")) {
        fwrite(report.data(), 1, report.size(), f);
        fclose(f);
    }
    return failures == 0 ? 0 : 1;
}
