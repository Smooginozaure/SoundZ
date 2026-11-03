#include "process_loopback.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <vector>

namespace {

// Définitions de audioclientactivationparams.h (absent des en-têtes de mingw).
enum ActivationType { kActivationDefault = 0, kActivationProcessLoopback = 1 };
enum LoopbackMode { kIncludeTree = 0, kExcludeTree = 1 };
struct ProcessLoopbackParams {
    DWORD targetProcessId;
    LoopbackMode mode;
};
struct ActivationParams {
    ActivationType type;
    ProcessLoopbackParams loopback;
};
const wchar_t kVirtualDevice[] = L"VAD\\Process_Loopback";

using ActivateFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*,
                                    IActivateAudioInterfaceAsyncOperation**);

// Reçoit la fin de l'activation. « Agile » : Windows peut l'appeler depuis n'importe quel thread.
class Completion final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    Completion() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Completion() { CloseHandle(done_); }
    HANDLE done() const { return done_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (iid == __uuidof(IAgileObject)) {
            *out = static_cast<IAgileObject*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return ULONG(n);
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(done_);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HANDLE done_;
};

}  // namespace

bool ProcessLoopback::start(DWORD processId, bool exclude, Callback callback, void* user) {
    stop();
    processId_ = processId;
    exclude_ = exclude;
    callback_ = callback;
    user_ = user;
    stop_ = false;
    ok_ = false;
    failed_ = false;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, threadMain, this, 0, nullptr);
    if (!thread_) {
        CloseHandle(ready_);
        ready_ = nullptr;
        return false;
    }
    WaitForSingleObject(ready_, 5000);
    if (!ok_) {
        stop();
        return false;
    }
    return true;
}

void ProcessLoopback::stop() {
    if (thread_) {
        stop_ = true;
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (ready_) {
        CloseHandle(ready_);
        ready_ = nullptr;
    }
}

DWORD WINAPI ProcessLoopback::threadMain(LPVOID self) {
    static_cast<ProcessLoopback*>(self)->run();
    return 0;
}

void ProcessLoopback::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    HANDLE sampleReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bool started = false;

    // 1. Activation du périphérique virtuel « capture par programme ».
    HMODULE mmdevapi = LoadLibraryW(L"Mmdevapi.dll");
    auto activate = mmdevapi ? reinterpret_cast<ActivateFn>(GetProcAddress(mmdevapi, "ActivateAudioInterfaceAsync")) : nullptr;
    if (activate) {
        ActivationParams params = {kActivationProcessLoopback, {processId_, exclude_ ? kExcludeTree : kIncludeTree}};
        PROPVARIANT variant = {};
        variant.vt = VT_BLOB;
        variant.blob.cbSize = sizeof(params);
        variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
        Completion* completion = new Completion();
        IActivateAudioInterfaceAsyncOperation* operation = nullptr;
        if (SUCCEEDED(activate(kVirtualDevice, __uuidof(IAudioClient), &variant, completion, &operation)) &&
            WaitForSingleObject(completion->done(), 5000) == WAIT_OBJECT_0) {
            HRESULT result = E_FAIL;
            IUnknown* unknown = nullptr;
            if (SUCCEEDED(operation->GetActivateResult(&result, &unknown)) && SUCCEEDED(result) && unknown) {
                unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client));
                unknown->Release();
            }
        }
        if (operation) operation->Release();
        completion->Release();
    }

    // 2. Format imposé (ce périphérique n'en propose pas) : 48 kHz, stéréo, flottants.
    if (client) {
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        started = SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000 /* 200 ms */, 0, &format, nullptr)) &&
                  SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture))) &&
                  SUCCEEDED(client->SetEventHandle(sampleReady)) && SUCCEEDED(client->Start());
    }
    ok_ = started;
    SetEvent(ready_);

    // 3. Les paquets de son, au fil de l'eau.
    std::vector<float> silence;
    while (started && !stop_) {
        WaitForSingleObject(sampleReady, 100);
        UINT32 packet = 0;
        while (!stop_) {
            HRESULT hr = capture->GetNextPacketSize(&packet);
            if (FAILED(hr)) {
                failed_ = true;
                started = false;
                break;
            }
            if (packet == 0) break;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD bufferFlags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr))) break;
            const float* samples = reinterpret_cast<const float*>(data);
            if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                silence.assign(size_t(frames) * 2, 0.0f);
                samples = silence.data();
            }
            if (frames > 0) callback_(user_, samples, frames);
            capture->ReleaseBuffer(frames);
        }
    }

    if (client) client->Stop();
    if (capture) capture->Release();
    if (client) client->Release();
    CloseHandle(sampleReady);
    if (mmdevapi) FreeLibrary(mmdevapi);
    CoUninitialize();
}
