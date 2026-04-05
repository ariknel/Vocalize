#include "WasapiCapture.h"
#include "ThreadOptimizer.h"
#include <initguid.h>
#include <ksmedia.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
WasapiCapture::WasapiCapture()  = default;
WasapiCapture::~WasapiCapture() { close(); }

// ─────────────────────────────────────────────────────────────────────────────
// Enumerate capture devices
// ─────────────────────────────────────────────────────────────────────────────
std::vector<CaptureDeviceInfo> WasapiCapture::enumerateDevices()
{
    std::vector<CaptureDeviceInfo> result;

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&enumerator)))
        return result;

    // Get default device ID for flagging
    IMMDevice* defaultDev = nullptr;
    std::wstring defaultId;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDev)))
    {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDev->GetId(&id))) { defaultId = id; CoTaskMemFree(id); }
        defaultDev->Release();
    }

    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection)))
    {
        enumerator->Release();
        return result;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* dev = nullptr;
        if (FAILED(collection->Item(i, &dev))) continue;

        CaptureDeviceInfo info;
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id))) { info.id = id; CoTaskMemFree(id); }

        IPropertyStore* props = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
        {
            PROPVARIANT pv; PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.pwszVal)
                info.name = pv.pwszVal;
            PropVariantClear(&pv);
            props->Release();
        }

        info.isDefault = (info.id == defaultId);
        result.push_back(std::move(info));
        dev->Release();
    }

    collection->Release();
    enumerator->Release();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// open() — the heart of the latency optimisation
// ─────────────────────────────────────────────────────────────────────────────
bool WasapiCapture::open(const std::wstring& deviceId, AudioRingBuffer& ring, int mode)
{
    close();
    m_ring = &ring;
    ring.reset();

    // ── Get device ───────────────────────────────────────────────────────────
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&enumerator)))
    {
        setError("CoCreateInstance(MMDeviceEnumerator) failed");
        return false;
    }

    HRESULT hr;
    if (deviceId.empty())
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_device);
    else
        hr = enumerator->GetDevice(deviceId.c_str(), &m_device);
    enumerator->Release();

    if (FAILED(hr) || !m_device)
    {
        setError("Failed to get capture device - check Windows mic privacy settings");
        return false;
    }

    // ── Get mix format ───────────────────────────────────────────────────────
    // We must get the format before activating IAudioClient3
    // Use a temporary IAudioClient to get format, then use IAudioClient3
    IAudioClient* tempClient = nullptr;
    WAVEFORMATEX* wfx = nullptr;

    if (FAILED(m_device->Activate(__uuidof(IAudioClient),
                                   CLSCTX_ALL, nullptr, (void**)&tempClient)) ||
        FAILED(tempClient->GetMixFormat(&wfx)))
    {
        if (tempClient) tempClient->Release();
        setError("GetMixFormat failed");
        return false;
    }
    tempClient->Release();

    // Parse native format
    m_nativeSr    = wfx->nSamplesPerSec;
    m_nativeCh    = wfx->nChannels;
    m_blockAlign  = wfx->nBlockAlign;
    m_bitDepth    = wfx->wBitsPerSample;
    m_isFloat     = false;

    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx);
        m_isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        m_isFloat = true;

    // ── Create event handle ──────────────────────────────────────────────────
    m_eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) { CoTaskMemFree(wfx); setError("CreateEvent failed"); return false; }

    // ── Mode 1: Exclusive ────────────────────────────────────────────────────
    if (mode == 1)
    {
        if (FAILED(m_device->Activate(__uuidof(IAudioClient),
                                       CLSCTX_ALL, nullptr, (void**)&m_audioClient)))
        {
            CoTaskMemFree(wfx);
            setError("Activate IAudioClient (exclusive) failed");
            return false;
        }

        // Target: 3ms exclusive period
        const REFERENCE_TIME targetPeriod = static_cast<REFERENCE_TIME>(
            m_nativeSr > 0 ? (3.0 / 1000.0 * m_nativeSr) : 144) * 10000000LL /
            static_cast<REFERENCE_TIME>(m_nativeSr > 0 ? m_nativeSr : 48000);
        // Clamp: at least 1ms, at most 10ms
        const REFERENCE_TIME minRef = 10000;   // 1ms in 100ns units
        const REFERENCE_TIME maxRef = 100000;  // 10ms
        const REFERENCE_TIME period = std::max(minRef, std::min(maxRef, targetPeriod));

        hr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            period, period, wfx, nullptr);

        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
        {
            // Try 16-bit PCM as fallback for exclusive
            WAVEFORMATEX wfxPCM = *wfx;
            wfxPCM.wFormatTag      = WAVE_FORMAT_PCM;
            wfxPCM.wBitsPerSample  = 16;
            wfxPCM.nBlockAlign     = (wfxPCM.nChannels * wfxPCM.wBitsPerSample) / 8;
            wfxPCM.nAvgBytesPerSec = wfxPCM.nSamplesPerSec * wfxPCM.nBlockAlign;
            hr = m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                period, period, &wfxPCM, nullptr);
            if (SUCCEEDED(hr))
            {
                m_bitDepth   = 16;
                m_isFloat    = false;
                m_blockAlign = wfxPCM.nBlockAlign;
            }
        }

        if (FAILED(hr))
        {
            CoTaskMemFree(wfx);
            if (hr == AUDCLNT_E_DEVICE_IN_USE)
                setError("Exclusive mode unavailable - another app is using this device");
            else
                setError("Exclusive Initialize failed (HRESULT " +
                          std::to_string((unsigned long)hr) + ")");
            return false;
        }

        float actualPeriodMs = static_cast<float>(period / 10000.0);
        m_actualPeriodMs.store(actualPeriodMs);
        m_isExclusive.store(true);
        m_usedIAC3.store(false);
    }
    else
    {
        // ── Mode 0: Shared (IAC3 → IAC1 fallback) ───────────────────────────
        m_isExclusive.store(false);

        // Try IAudioClient3 first
        IAudioClient3* ac3 = nullptr;
        bool iac3Success = false;

        if (SUCCEEDED(m_device->Activate(__uuidof(IAudioClient3),
                                          CLSCTX_ALL, nullptr, (void**)&ac3)))
        {
            AudioClientProperties acProps = {};
            acProps.cbSize     = sizeof(acProps);
            acProps.bIsOffload = FALSE;
            acProps.eCategory  = AudioCategory_Media;
            acProps.Options    = AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;
            ac3->SetClientProperties(&acProps);

            UINT32 defPeriod = 0, fundPeriod = 0, minPeriod = 0, maxPeriod = 0;
            if (SUCCEEDED(ac3->GetSharedModeEnginePeriod(
                    wfx, &defPeriod, &fundPeriod, &minPeriod, &maxPeriod)) &&
                minPeriod < defPeriod)
            {
                // Driver supports small shared buffers — use minimum
                hr = ac3->InitializeSharedAudioStream(
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    minPeriod, wfx, nullptr);

                if (SUCCEEDED(hr))
                {
                    m_audioClient = ac3;
                    iac3Success   = true;
                    m_usedIAC3.store(true);

                    // Query actual achieved period
                    UINT32 currentPeriod = 0;
                    ac3->GetCurrentSharedModeEnginePeriod(wfx, &currentPeriod);
                    if (currentPeriod == 0) currentPeriod = minPeriod;
                    m_actualPeriodMs.store(
                        static_cast<float>(currentPeriod) / m_nativeSr * 1000.0f);
                }
            }

            if (!iac3Success) ac3->Release();
        }

        if (!iac3Success)
        {
            // Fall back to standard IAudioClient (10ms shared)
            m_usedIAC3.store(false);
            if (FAILED(m_device->Activate(__uuidof(IAudioClient),
                                           CLSCTX_ALL, nullptr, (void**)&m_audioClient)))
            {
                CoTaskMemFree(wfx);
                setError("Activate IAudioClient (shared fallback) failed");
                return false;
            }

            const REFERENCE_TIME bufDuration = 100000; // 10ms
            hr = m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                bufDuration, 0, wfx, nullptr);

            if (FAILED(hr))
            {
                CoTaskMemFree(wfx);
                setError("IAudioClient::Initialize (shared) failed: " +
                          std::to_string((unsigned long)hr));
                return false;
            }

            // Default 10ms period
            m_actualPeriodMs.store(
                static_cast<float>(m_nativeSr) > 0
                ? static_cast<float>(480) / m_nativeSr * 1000.0f
                : 10.0f);
        }
    }

    // ── Set event handle + get capture client ────────────────────────────────
    hr = m_audioClient->SetEventHandle(m_eventHandle);
    if (FAILED(hr)) { CoTaskMemFree(wfx); setError("SetEventHandle failed"); return false; }

    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient),
                                    (void**)&m_captureClient);
    if (FAILED(hr)) { CoTaskMemFree(wfx); setError("GetService(CaptureClient) failed"); return false; }

    // ── Stream latency ───────────────────────────────────────────────────────
    REFERENCE_TIME latency = 0;
    if (SUCCEEDED(m_audioClient->GetStreamLatency(&latency)))
        m_streamLatencyMs.store(static_cast<float>(latency / 10000.0));

    // ── Pre-allocate conversion buffers ─────────────────────────────────────
    const size_t prealloc = 4096;
    m_convBuf.resize(prealloc * m_nativeCh, 0.0f);
    m_stereoBuf.resize(prealloc * 2, 0.0f);

    CoTaskMemFree(wfx);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Start / Stop / Close
// ─────────────────────────────────────────────────────────────────────────────
void WasapiCapture::start()
{
    if (m_running.load() || !m_audioClient) return;
    m_stopFlag.store(false);
    m_running.store(true);
    m_audioClient->Start();
    m_thread = std::thread(&WasapiCapture::captureThread, this);
}

void WasapiCapture::stop()
{
    if (!m_running.load()) return;
    m_stopFlag.store(true);
    if (m_eventHandle) SetEvent(m_eventHandle);
    if (m_thread.joinable()) m_thread.join();
    if (m_audioClient) m_audioClient->Stop();
    m_running.store(false);
}

void WasapiCapture::close()
{
    stop();
    if (m_captureClient) { m_captureClient->Release(); m_captureClient = nullptr; }
    if (m_audioClient)   { m_audioClient->Release();   m_audioClient   = nullptr; }
    if (m_device)        { m_device->Release();         m_device        = nullptr; }
    if (m_eventHandle)   { CloseHandle(m_eventHandle);  m_eventHandle   = nullptr; }
    m_ring = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture thread — the "express lane"
// Applies all three thread optimisations on entry.
// ─────────────────────────────────────────────────────────────────────────────
void WasapiCapture::captureThread()
{
    // ── Apply all thread optimisations ───────────────────────────────────────
    // 1. MMCSS "Pro Audio" registration
    // 2. THREAD_PRIORITY_TIME_CRITICAL
    // 3. P-Core affinity lock (if hybrid CPU detected)
    ThreadOptResult opts = applyThreadOptimisations();

    {
        std::lock_guard<std::mutex> lk(m_optMu);
        m_threadOptSummary = opts.summary;
    }

    // ── Capture loop ─────────────────────────────────────────────────────────
    while (!m_stopFlag.load(std::memory_order_relaxed))
    {
        DWORD wr = WaitForSingleObject(m_eventHandle, 200);
        if (wr != WAIT_OBJECT_0) continue;
        if (m_stopFlag.load(std::memory_order_relaxed)) break;

        UINT32 packetSize = 0;
        while (SUCCEEDED(m_captureClient->GetNextPacketSize(&packetSize))
               && packetSize > 0)
        {
            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            if (FAILED(m_captureClient->GetBuffer(&data, &frames, &flags,
                                                   nullptr, nullptr)))
                break;

            processPacket(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
            m_captureClient->ReleaseBuffer(frames);
        }
    }

    // MMCSS release is handled by OS when thread exits (AvRevertMmThreadCharacteristics
    // is optional if thread is ending — OS cleans up automatically)
}

// ─────────────────────────────────────────────────────────────────────────────
// Process one captured packet
// ─────────────────────────────────────────────────────────────────────────────
void WasapiCapture::processPacket(const BYTE* data, UINT32 frames, bool silent)
{
    if (frames == 0 || !m_ring) return;

    const size_t needed = static_cast<size_t>(frames) * m_nativeCh;
    if (m_convBuf.size() < needed) m_convBuf.resize(needed * 2);

    float* conv = m_convBuf.data();

    if (silent)
        std::memset(conv, 0, needed * sizeof(float));
    else
        convertToStereoFloat(data, conv, frames);

    // Normalise to stereo
    const size_t stereoSamples = static_cast<size_t>(frames) * 2;
    if (m_stereoBuf.size() < stereoSamples) m_stereoBuf.resize(stereoSamples * 2);
    float* stereo = m_stereoBuf.data();

    if (m_nativeCh == 1)
    {
        for (size_t i = 0; i < frames; ++i)
        {
            stereo[i * 2]     = conv[i];
            stereo[i * 2 + 1] = conv[i];
        }
    }
    else if (m_nativeCh == 2)
        std::memcpy(stereo, conv, stereoSamples * sizeof(float));
    else // > 2 channels: take L+R only
    {
        for (size_t i = 0; i < frames; ++i)
        {
            stereo[i * 2]     = conv[i * m_nativeCh];
            stereo[i * 2 + 1] = conv[i * m_nativeCh + 1];
        }
    }

    updateLevels(stereo, frames);
    m_ring->write(stereo, frames);
}

// ─────────────────────────────────────────────────────────────────────────────
// Format conversion
// ─────────────────────────────────────────────────────────────────────────────
void WasapiCapture::convertToStereoFloat(const BYTE* src, float* dst, UINT32 frames)
{
    const UINT32 samples = frames * m_nativeCh;

    if (m_isFloat)
    {
        std::memcpy(dst, src, samples * sizeof(float));
        return;
    }

    if (m_bitDepth == 16)
    {
        constexpr float kScale = 1.0f / 32768.0f;
        auto* s = reinterpret_cast<const int16_t*>(src);
        for (UINT32 i = 0; i < samples; ++i) dst[i] = s[i] * kScale;
    }
    else if (m_bitDepth == 24)
    {
        constexpr float kScale = 1.0f / 8388608.0f;
        const uint8_t* s = src;
        for (UINT32 i = 0; i < samples; ++i)
        {
            int32_t v = (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16);
            v = (v << 8) >> 8; // sign extend
            dst[i] = v * kScale;
            s += 3;
        }
    }
    else // 32-bit int
    {
        constexpr float kScale = 1.0f / 2147483648.0f;
        auto* s = reinterpret_cast<const int32_t*>(src);
        for (UINT32 i = 0; i < samples; ++i) dst[i] = s[i] * kScale;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Level metering — exponential smoothing, thread-safe atomics
// ─────────────────────────────────────────────────────────────────────────────
void WasapiCapture::updateLevels(const float* stereo, UINT32 frames)
{
    if (frames == 0) return;
    float sumL = 0.0f, sumR = 0.0f;
    for (UINT32 i = 0; i < frames; ++i)
    {
        sumL += stereo[i * 2]     * stereo[i * 2];
        sumR += stereo[i * 2 + 1] * stereo[i * 2 + 1];
    }
    const float inv  = 1.0f / static_cast<float>(frames);
    const float rmsL = std::sqrt(sumL * inv);
    const float rmsR = std::sqrt(sumR * inv);
    constexpr float a = 0.15f; // smoothing factor
    m_levelL.store(m_levelL.load(std::memory_order_relaxed) * (1.0f - a) + rmsL * a,
                   std::memory_order_relaxed);
    m_levelR.store(m_levelR.load(std::memory_order_relaxed) * (1.0f - a) + rmsR * a,
                   std::memory_order_relaxed);
}
