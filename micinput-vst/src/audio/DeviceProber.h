#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DeviceProber — pure functions, no state, no JUCE dependency
//
// Called once on device selection (message thread).
// Results stored in processor atomics and displayed in GUI.
// All probes are non-destructive: any WASAPI sessions opened are
// immediately closed after measurement.
// ─────────────────────────────────────────────────────────────────────────────
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <string>
#include <vector>

struct DeviceProfile
{
    // ── What we measured ────────────────────────────────────────────────────
    float   sharedDefaultPeriodMs  = 10.0f;
    float   sharedMinPeriodMs      = 10.0f;
    bool    supportsIAC3SmallBuf   = false;  // minPeriod < defaultPeriod
    bool    supportsExclusive      = false;
    float   exclusiveMinPeriodMs   = 3.0f;
    float   streamLatencyMs        = 10.0f;  // from IAudioClient::GetStreamLatency

    // ── What we detected ───────────────────────────────────────────────────
    bool        isUsbDevice        = false;
    bool        isGenericDriver    = false;  // usbaudio.sys or usbaudio2.sys
    std::wstring driverName;                 // e.g. L"usbaudio2.sys"
    std::wstring deviceName;

    // ── Convenience ────────────────────────────────────────────────────────
    float bestSharedPeriodMs() const
    {
        return supportsIAC3SmallBuf ? sharedMinPeriodMs : sharedDefaultPeriodMs;
    }
};

struct OutputProfile
{
    bool         isUsb            = false;
    bool         isBluetooth      = false;
    float        latencyMs        = 1.5f;    // 1.5 USB, 0.5 analog, 150+ BT
    std::wstring deviceName;
    std::vector<std::wstring> activeAppNames;  // for exclusive warning
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

inline std::wstring getPropString(IPropertyStore* props, const PROPERTYKEY& key)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(props->GetValue(key, &pv)) && pv.pwszVal)
    {
        std::wstring s = pv.pwszVal;
        PropVariantClear(&pv);
        return s;
    }
    PropVariantClear(&pv);
    return L"";
}

inline bool containsCI(const std::wstring& s, const wchar_t* sub)
{
    // case-insensitive wstring search
    std::wstring sl = s, subl = sub;
    for (auto& c : sl)   c = towlower(c);
    for (auto& c : subl) c = towlower(c);
    return sl.find(subl) != std::wstring::npos;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// Probe a capture device — call on message thread, device already selected
// ─────────────────────────────────────────────────────────────────────────────
inline DeviceProfile probeDevice(IMMDevice* device)
{
    DeviceProfile p;
    if (!device) return p;

    // ── Device name + driver detection ────────────────────────────────────
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)))
    {
        p.deviceName  = detail::getPropString(props, PKEY_Device_FriendlyName);
        p.driverName  = detail::getPropString(props, PKEY_Device_Driver);
        std::wstring enumerator = detail::getPropString(props, PKEY_Device_EnumeratorName);
        p.isUsbDevice = detail::containsCI(enumerator, L"USB");

        // Generic Microsoft USB audio drivers
        p.isGenericDriver = detail::containsCI(p.driverName, L"usbaudio");
        props->Release();
    }

    // ── IAudioClient3 period probe ────────────────────────────────────────
    IAudioClient3* ac3 = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioClient3),
                                    CLSCTX_ALL, nullptr, (void**)&ac3)))
    {
        WAVEFORMATEX* wfx = nullptr;
        if (SUCCEEDED(ac3->GetMixFormat(&wfx)))
        {
            // SetClientProperties — required to unlock small shared periods
            AudioClientProperties props2 = {};
            props2.cbSize     = sizeof(props2);
            props2.bIsOffload = FALSE;
            props2.eCategory  = AudioCategory_Media;
            props2.Options    = AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;
            ac3->SetClientProperties(&props2);

            UINT32 defPeriod = 0, fundPeriod = 0, minPeriod = 0, maxPeriod = 0;
            if (SUCCEEDED(ac3->GetSharedModeEnginePeriod(
                    wfx, &defPeriod, &fundPeriod, &minPeriod, &maxPeriod)))
            {
                double sr = wfx->nSamplesPerSec;
                p.sharedDefaultPeriodMs = static_cast<float>(defPeriod / sr * 1000.0);
                p.sharedMinPeriodMs     = static_cast<float>(minPeriod / sr * 1000.0);
                p.supportsIAC3SmallBuf  = (minPeriod < defPeriod);
            }

            // Probe stream latency via a quick standard shared Initialize
            IAudioClient* ac1 = nullptr;
            if (SUCCEEDED(device->Activate(__uuidof(IAudioClient),
                                            CLSCTX_ALL, nullptr, (void**)&ac1)))
            {
                const REFERENCE_TIME period = 100000; // 10ms
                if (SUCCEEDED(ac1->Initialize(AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                    period, 0, wfx, nullptr)))
                {
                    REFERENCE_TIME latency = 0;
                    if (SUCCEEDED(ac1->GetStreamLatency(&latency)))
                        p.streamLatencyMs = static_cast<float>(latency / 10000.0);
                }
                ac1->Release();
            }

            // Probe exclusive support (non-destructive)
            AudioClientProperties excProps = {};
            excProps.cbSize     = sizeof(excProps);
            excProps.bIsOffload = FALSE;
            excProps.eCategory  = AudioCategory_Media;
            // Try exclusive with smallest possible period (fundamental period)
            const REFERENCE_TIME excPeriod = static_cast<REFERENCE_TIME>(
                fundPeriod > 0 ? fundPeriod : 480) * 10000000LL /
                static_cast<REFERENCE_TIME>(wfx->nSamplesPerSec);

            IAudioClient* acExc = nullptr;
            if (SUCCEEDED(device->Activate(__uuidof(IAudioClient),
                                            CLSCTX_ALL, nullptr, (void**)&acExc)))
            {
                HRESULT hr = acExc->Initialize(
                    AUDCLNT_SHAREMODE_EXCLUSIVE,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    excPeriod, excPeriod, wfx, nullptr);

                p.supportsExclusive = SUCCEEDED(hr);
                if (p.supportsExclusive && wfx->nSamplesPerSec > 0)
                {
                    // Minimum exclusive period ≈ fundamental period
                    p.exclusiveMinPeriodMs = static_cast<float>(
                        (fundPeriod > 0 ? fundPeriod : 480)
                        / (double)wfx->nSamplesPerSec * 1000.0);
                }
                acExc->Release();
            }

            CoTaskMemFree(wfx);
        }
        ac3->Release();
    }

    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Probe the system's default render (output/headphone) device
// ─────────────────────────────────────────────────────────────────────────────
inline OutputProfile probeDefaultOutput()
{
    OutputProfile p;

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&enumerator)))
        return p;

    IMMDevice* dev = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
    {
        enumerator->Release();
        return p;
    }

    IPropertyStore* props = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
    {
        p.deviceName = detail::getPropString(props, PKEY_Device_FriendlyName);

        std::wstring enumeratorName = detail::getPropString(props, PKEY_Device_EnumeratorName);
        p.isUsb       = detail::containsCI(enumeratorName, L"USB");
        p.isBluetooth = detail::containsCI(enumeratorName, L"BTHENUM") ||
                        detail::containsCI(p.deviceName,   L"Bluetooth");

        if (p.isBluetooth)      p.latencyMs = 150.0f;
        else if (p.isUsb)       p.latencyMs = 1.5f;
        else                    p.latencyMs = 0.5f;   // analog 3.5mm / HDAudio

        props->Release();
    }

    // Detect active apps using this render device (for exclusive mode warning)
    IAudioSessionManager2* mgr = nullptr;
    if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2),
                                 CLSCTX_ALL, nullptr, (void**)&mgr)))
    {
        IAudioSessionEnumerator* sessions = nullptr;
        if (SUCCEEDED(mgr->GetSessionEnumerator(&sessions)))
        {
            int count = 0;
            sessions->GetCount(&count);
            for (int i = 0; i < count; ++i)
            {
                IAudioSessionControl* ctrl = nullptr;
                if (FAILED(sessions->GetSession(i, &ctrl))) continue;

                IAudioSessionControl2* ctrl2 = nullptr;
                if (SUCCEEDED(ctrl->QueryInterface(&ctrl2)))
                {
                    AudioSessionState state;
                    DWORD pid = 0;
                    if (SUCCEEDED(ctrl2->GetProcessId(&pid)) &&
                        SUCCEEDED(ctrl->GetState(&state)) &&
                        state == AudioSessionStateActive && pid != 0)
                    {
                        HANDLE hProc = OpenProcess(
                            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                        if (hProc)
                        {
                            wchar_t exePath[MAX_PATH] = {};
                            DWORD sz = MAX_PATH;
                            if (QueryFullProcessImageNameW(hProc, 0, exePath, &sz))
                            {
                                std::wstring path = exePath;
                                // Extract exe name
                                auto slash = path.find_last_of(L"\\/");
                                std::wstring exe = (slash != std::wstring::npos)
                                    ? path.substr(slash + 1) : path;
                                // Map to friendly names
                                struct { const wchar_t* exe; const wchar_t* name; } known[] = {
                                    {L"Discord.exe",    L"Discord"},
                                    {L"chrome.exe",     L"Chrome / YouTube"},
                                    {L"spotify.exe",    L"Spotify"},
                                    {L"msedge.exe",     L"Microsoft Edge"},
                                    {L"Teams.exe",      L"Microsoft Teams"},
                                    {L"obs64.exe",      L"OBS Studio"},
                                    {L"firefox.exe",    L"Firefox"},
                                    {L"vlc.exe",        L"VLC"},
                                    {L"Zoom.exe",       L"Zoom"},
                                };
                                for (auto& k : known)
                                {
                                    if (detail::containsCI(exe, k.exe))
                                    {
                                        p.activeAppNames.push_back(k.name);
                                        break;
                                    }
                                }
                            }
                            CloseHandle(hProc);
                        }
                    }
                    ctrl2->Release();
                }
                ctrl->Release();
            }
            sessions->Release();
        }
        mgr->Release();
    }

    dev->Release();
    enumerator->Release();
    return p;
}
