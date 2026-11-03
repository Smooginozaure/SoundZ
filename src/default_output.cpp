#include "default_output.h"

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propsys.h>

#include <cmath>

namespace {

// Interface non documentée de Windows, utilisée par le panneau Son lui-même
// (et par des outils comme EarTrumpet ou SoundSwitch) pour changer la sortie par défaut.
const CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
const IID kIPolicyConfig = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

struct IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, BOOL fxStore, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, BOOL fxStore, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

// PKEY_AudioEndpoint_Disable_SysFx : la case « Désactiver toutes les améliorations ».
const PROPERTYKEY kDisableSysFx = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 5};

IPolicyConfig* openPolicy() {
    IPolicyConfig* policy = nullptr;
    CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, kIPolicyConfig, reinterpret_cast<void**>(&policy));
    return policy;
}

IMMDeviceEnumerator* openEnumerator() {
    IMMDeviceEnumerator* enumerator = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                     reinterpret_cast<void**>(&enumerator));
    return enumerator;
}

}  // namespace

std::wstring getDefaultOutput() {
    std::wstring result;
    IMMDeviceEnumerator* enumerator = openEnumerator();
    if (!enumerator) return result;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            result = id;
            CoTaskMemFree(id);
        }
        device->Release();
    }
    enumerator->Release();
    return result;
}

bool setDefaultOutput(const std::wstring& endpointId) {
    IPolicyConfig* policy = openPolicy();
    if (!policy) return false;
    bool ok = SUCCEEDED(policy->SetDefaultEndpoint(endpointId.c_str(), eConsole)) &&
              SUCCEEDED(policy->SetDefaultEndpoint(endpointId.c_str(), eMultimedia));
    policy->Release();
    return ok;
}

int getEffectsDisabled(const std::wstring& endpointId) {
    IPolicyConfig* policy = openPolicy();
    if (!policy) return -1;
    PROPVARIANT value;
    PropVariantInit(&value);
    int result = -1;
    if (SUCCEEDED(policy->GetPropertyValue(endpointId.c_str(), TRUE, kDisableSysFx, &value)) && value.vt == VT_UI4) {
        result = value.ulVal ? 1 : 0;
    }
    PropVariantClear(&value);
    policy->Release();
    return result;
}

bool setEffectsDisabled(const std::wstring& endpointId, bool disabled) {
    IPolicyConfig* policy = openPolicy();
    if (!policy) return false;
    PROPVARIANT value;
    PropVariantInit(&value);
    value.vt = VT_UI4;
    value.ulVal = disabled ? 1 : 0;
    bool ok = SUCCEEDED(policy->SetPropertyValue(endpointId.c_str(), TRUE, kDisableSysFx, &value));
    policy->Release();
    return ok;
}

bool EndpointVolume::open(const std::wstring& endpointId) {
    close();
    IMMDeviceEnumerator* enumerator = openEnumerator();
    if (!enumerator) return false;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDevice(endpointId.c_str(), &device))) {
        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&volume_));
        device->Release();
    }
    enumerator->Release();
    return volume_ != nullptr;
}

void EndpointVolume::close() {
    if (volume_) volume_->Release();
    volume_ = nullptr;
}

bool EndpointVolume::get(float& scalar, bool& muted) {
    if (!volume_) return false;
    BOOL mute = FALSE;
    bool ok = SUCCEEDED(volume_->GetMasterVolumeLevelScalar(&scalar)) && SUCCEEDED(volume_->GetMute(&mute));
    muted = mute != FALSE;
    return ok;
}

bool EndpointVolume::set(float scalar, bool muted) {
    if (!volume_) return false;
    return SUCCEEDED(volume_->SetMasterVolumeLevelScalar(scalar, nullptr)) &&
           SUCCEEDED(volume_->SetMute(muted ? TRUE : FALSE, nullptr));
}

float EndpointVolume::gain() {
    float scalar = 1;
    bool muted = false;
    if (!get(scalar, muted)) return 1;
    if (muted || scalar <= 0.0001f) return 0;
    // Même courbe que celle que Windows appliquerait : on lit la valeur en décibels.
    float db = 0;
    if (FAILED(volume_->GetMasterVolumeLevel(&db))) return scalar;
    return std::pow(10.0f, db / 20.0f);
}

std::wstring getDefaultDevice(bool capture, int role) {
    std::wstring result;
    IMMDeviceEnumerator* enumerator = openEnumerator();
    if (!enumerator) return result;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(capture ? eCapture : eRender, ERole(role), &device))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            result = id;
            CoTaskMemFree(id);
        }
        device->Release();
    }
    enumerator->Release();
    return result;
}

bool setDefaultDevice(const std::wstring& endpointId, int role) {
    IPolicyConfig* policy = openPolicy();
    if (!policy) return false;
    bool ok = SUCCEEDED(policy->SetDefaultEndpoint(endpointId.c_str(), ERole(role)));
    policy->Release();
    return ok;
}

bool isPersonalDevice(const std::wstring& endpointId) {
    // Valeurs de EndpointFormFactor : 1 enceintes, 3 casque, 4 micro, 5 micro-casque.
    const PROPERTYKEY formFactor = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};
    bool personal = false;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return false;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDevice(endpointId.c_str(), &device))) {
        IPropertyStore* store = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(formFactor, &value)) && value.vt == VT_UI4)
                personal = value.ulVal == 1 || value.ulVal == 3 || value.ulVal == 4 || value.ulVal == 5;
            PropVariantClear(&value);
            store->Release();
        }
        device->Release();
    }
    enumerator->Release();
    return personal;
}
