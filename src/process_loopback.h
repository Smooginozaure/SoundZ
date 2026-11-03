// Capture « par programme » de Windows (10 version 2004 et plus récent) : le son de tous les programmes sauf
// un (ou d'un seul), quelle que soit la sortie qu'ils utilisent. Sert au soundboard pour entendre YouTube,
// Discord, les jeux… sans entendre SoundZ lui-même (musique transposée, sons du soundboard, bips).
// Fournit du 48 kHz stéréo en flottants, depuis un thread à lui.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

class ProcessLoopback {
public:
    using Callback = void (*)(void* user, const float* stereo, uint32_t frames);

    ~ProcessLoopback() { stop(); }

    // exclude = true : tous les programmes sauf processId (et ses enfants).
    bool start(DWORD processId, bool exclude, Callback callback, void* user);
    void stop();
    bool running() const { return thread_ != nullptr; }
    bool failed() const { return failed_.load(); }  // le flux s'est interrompu

private:
    static DWORD WINAPI threadMain(LPVOID self);
    void run();

    HANDLE thread_ = nullptr;
    HANDLE ready_ = nullptr;
    std::atomic<bool> stop_{false}, ok_{false}, failed_{false};
    DWORD processId_ = 0;
    bool exclude_ = true;
    Callback callback_ = nullptr;
    void* user_ = nullptr;
};
