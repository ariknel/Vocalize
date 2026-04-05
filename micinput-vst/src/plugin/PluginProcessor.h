#pragma once
#include <JuceHeader.h>
#include "audio/WasapiCapture.h"
#include "audio/AudioRingBuffer.h"
#include "audio/DeviceProber.h"
#include "plugin/Parameters.h"

class MicInputProcessor : public juce::AudioProcessor
{
public:
    MicInputProcessor();
    ~MicInputProcessor() override;

    // ── AudioProcessor ────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MicInput"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── Device management (message thread only) ───────────────────────────
    std::vector<CaptureDeviceInfo> getAvailableDevices() const;
    void selectDevice(int index);   // -1 = system default
    void setMode(int mode);         // 0 = shared, 1 = exclusive
    void refreshDevices();

    // ── Runtime state (atomic — any thread) ───────────────────────────────
    std::atomic<bool>     isCapturing{false};
    std::atomic<float>    levelL{0.f};
    std::atomic<float>    levelR{0.f};
    std::atomic<float>    captureMs{10.f};
    std::atomic<float>    streamLatMs{0.f};
    std::atomic<float>    outputLatMs{1.5f};
    std::atomic<float>    totalLatMs{26.f};
    std::atomic<float>    blockMs{2.7f};
    std::atomic<bool>     usedIAC3{false};
    std::atomic<bool>     isExclusive{false};
    std::atomic<bool>     outputIsUsb{true};
    std::atomic<bool>     outputIsBluetooth{false};
    std::atomic<uint64_t> underruns{0};

    // Thread optimisation summary (set after capture starts)
    std::string getThreadOptSummary() const { return m_capture.threadOptSummary(); }
    std::string getCaptureError()     const { return m_capture.lastError(); }

    // Device profiles (set after selectDevice, message thread)
    DeviceProfile  currentDeviceProfile;
    OutputProfile  currentOutputProfile;

    // APVTS (public so editor can attach)
    juce::AudioProcessorValueTreeState apvts;

private:
    void openCapture();
    void closeCapture();
    void recalcTotalLatency();

    WasapiCapture  m_capture;
    AudioRingBuffer m_ring{24000};  // ~500ms @ 48kHz

    std::vector<CaptureDeviceInfo> m_devices;
    mutable std::mutex             m_devicesMu;
    std::string                    m_selectedDeviceId;
    int                            m_captureMode = 0;
    int                            m_selectedDeviceIndex = -1;

    double m_sampleRate = 48000.0;
    int    m_blockSize  = 512;

    // Safety margin: consume only when ring has > this many frames
    // Prevents clicks from consuming below the capture thread's period
    size_t m_safetyFrames = 480;

    // processBlock interleaved scratch buffer
    // thread_local so it's per-calling-thread but pre-allocated
    static thread_local std::vector<float> s_interleavedBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MicInputProcessor)
};
