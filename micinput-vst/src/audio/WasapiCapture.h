#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WasapiCapture — WASAPI microphone capture with three modes:
//
//   Mode 0 (Shared Auto):
//     Tries IAudioClient3::InitializeSharedAudioStream(minPeriod)
//     Falls back to IAudioClient::Initialize(defaultPeriod) if IAC3 fails
//
//   Mode 1 (Exclusive):
//     IAudioClient::Initialize(EXCLUSIVE, targetPeriod)
//     Returns false and sets lastError if exclusive unavailable
//
// Thread model:
//   open()  / close()  — message thread
//   start() / stop()   — message thread
//   captureThread()    — internal, Pro Audio priority + P-core affinity
//   write ring         — capture thread
//   read ring          — audio thread (processBlock)
//   atomic accessors   — any thread
// ─────────────────────────────────────────────────────────────────────────────
#include "AudioRingBuffer.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

struct CaptureDeviceInfo
{
    std::wstring id;
    std::wstring name;
    bool         isDefault = false;
};

class WasapiCapture
{
public:
    WasapiCapture();
    ~WasapiCapture();

    static std::vector<CaptureDeviceInfo> enumerateDevices();

    // mode: 0 = shared (IAC3 auto), 1 = exclusive
    bool open(const std::wstring& deviceId, AudioRingBuffer& ring, int mode = 0);
    void start();
    void stop();
    void close();

    // ── Runtime state (safe from any thread) ──────────────────────────────
    bool    isRunning()         const { return m_running.load(); }
    float   actualPeriodMs()    const { return m_actualPeriodMs.load(); }
    float   streamLatencyMs()   const { return m_streamLatencyMs.load(); }
    bool    usedIAC3()          const { return m_usedIAC3.load(); }
    bool    isExclusive()       const { return m_isExclusive.load(); }
    float   levelL()            const { return m_levelL.load(std::memory_order_relaxed); }
    float   levelR()            const { return m_levelR.load(std::memory_order_relaxed); }
    std::string lastError()     const { std::lock_guard<std::mutex> l(m_errMu); return m_lastError; }

    // Thread optimisation results — set after capture thread starts
    std::string threadOptSummary() const {
        std::lock_guard<std::mutex> l(m_optMu); return m_threadOptSummary;
    }

private:
    void captureThread();
    void processPacket(const BYTE* data, UINT32 frames, bool silent);
    void convertToStereoFloat(const BYTE* src, float* dstStereo, UINT32 frames);
    void updateLevels(const float* stereo, UINT32 frames);
    void setError(const std::string& e) {
        std::lock_guard<std::mutex> l(m_errMu); m_lastError = e;
    }

    // WASAPI objects
    IMMDevice*           m_device         = nullptr;
    IAudioClient*        m_audioClient    = nullptr;   // may be IAC3 via QI
    IAudioCaptureClient* m_captureClient  = nullptr;
    HANDLE               m_eventHandle    = nullptr;

    // Native format
    UINT32 m_nativeCh    = 2;
    UINT32 m_nativeSr    = 48000;
    bool   m_isFloat     = true;
    UINT32 m_bitDepth    = 32;
    UINT32 m_blockAlign  = 8;

    // Ring buffer (not owned)
    AudioRingBuffer* m_ring = nullptr;

    // Conversion buffers (pre-allocated in open())
    std::vector<float> m_convBuf;
    std::vector<float> m_stereoBuf;

    // Thread
    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopFlag{false};

    // Runtime metrics
    std::atomic<float> m_actualPeriodMs{10.0f};
    std::atomic<float> m_streamLatencyMs{0.0f};
    std::atomic<bool>  m_usedIAC3{false};
    std::atomic<bool>  m_isExclusive{false};
    std::atomic<float> m_levelL{0.0f};
    std::atomic<float> m_levelR{0.0f};

    // Error + opt strings
    mutable std::mutex m_errMu;
    std::string        m_lastError;
    mutable std::mutex m_optMu;
    std::string        m_threadOptSummary;
};
