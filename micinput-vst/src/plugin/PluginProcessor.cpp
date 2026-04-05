#include "plugin/PluginProcessor.h"
#include "gui/PluginEditor.h"

thread_local std::vector<float> MicInputProcessor::s_interleavedBuf;

// ─────────────────────────────────────────────────────────────────────────────
// Bus layout: no inputs, one stereo output
// ─────────────────────────────────────────────────────────────────────────────
static juce::AudioProcessor::BusesProperties makeBuses()
{
    return juce::AudioProcessor::BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
MicInputProcessor::MicInputProcessor()
    : AudioProcessor(makeBuses())
    , apvts(*this, nullptr, "MicInputState", MicInput::Params::createLayout())
{
    refreshDevices();

    // Probe output device immediately for latency display
    currentOutputProfile = probeDefaultOutput();
    outputIsUsb.store(currentOutputProfile.isUsb);
    outputIsBluetooth.store(currentOutputProfile.isBluetooth);
    outputLatMs.store(currentOutputProfile.latencyMs);

    openCapture();
}

MicInputProcessor::~MicInputProcessor()
{
    closeCapture();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device management
// ─────────────────────────────────────────────────────────────────────────────
std::vector<CaptureDeviceInfo> MicInputProcessor::getAvailableDevices() const
{
    std::lock_guard<std::mutex> lk(m_devicesMu);
    return m_devices;
}

void MicInputProcessor::refreshDevices()
{
    auto devices = WasapiCapture::enumerateDevices();
    std::lock_guard<std::mutex> lk(m_devicesMu);
    m_devices = std::move(devices);
}

void MicInputProcessor::selectDevice(int index)
{
    {
        std::lock_guard<std::mutex> lk(m_devicesMu);
        m_selectedDeviceIndex = index;
        if (index >= 0 && index < (int)m_devices.size())
            m_selectedDeviceId = juce::String(m_devices[index].id.c_str()).toStdString();
        else
            m_selectedDeviceId = "";
    }

    // Probe the selected device (message thread — blocking OK here)
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                    CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    (void**)&enumerator)))
    {
        IMMDevice* dev = nullptr;
        if (m_selectedDeviceId.empty())
            enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
        else
        {
            std::wstring wid(m_selectedDeviceId.begin(), m_selectedDeviceId.end());
            enumerator->GetDevice(wid.c_str(), &dev);
        }

        if (dev)
        {
            currentDeviceProfile = probeDevice(dev);
            dev->Release();
        }
        enumerator->Release();
    }

    closeCapture();
    openCapture();

    // Re-probe output after device change
    currentOutputProfile = probeDefaultOutput();
    outputIsUsb.store(currentOutputProfile.isUsb);
    outputIsBluetooth.store(currentOutputProfile.isBluetooth);
    outputLatMs.store(currentOutputProfile.latencyMs);
    recalcTotalLatency();
}

void MicInputProcessor::setMode(int mode)
{
    m_captureMode = mode;
    closeCapture();
    openCapture();
    recalcTotalLatency();
}

void MicInputProcessor::openCapture()
{
    std::wstring wid;
    {
        std::lock_guard<std::mutex> lk(m_devicesMu);
        wid = std::wstring(m_selectedDeviceId.begin(), m_selectedDeviceId.end());
    }

    m_ring.reset();

    if (m_capture.open(wid, m_ring, m_captureMode))
    {
        m_capture.start();
        isCapturing.store(true);

        captureMs.store(m_capture.actualPeriodMs());
        streamLatMs.store(m_capture.streamLatencyMs());
        usedIAC3.store(m_capture.usedIAC3());
        isExclusive.store(m_capture.isExclusive());

        // Safety margin = 1x the actual capture period
        // Prevents consuming into partially-filled blocks
        m_safetyFrames = static_cast<size_t>(
            m_capture.actualPeriodMs() / 1000.0f * static_cast<float>(m_sampleRate));
        m_safetyFrames = std::max(m_safetyFrames, static_cast<size_t>(32));

        recalcTotalLatency();
    }
    else
    {
        isCapturing.store(false);
    }
}

void MicInputProcessor::closeCapture()
{
    m_capture.stop();
    m_capture.close();
    isCapturing.store(false);
}

void MicInputProcessor::recalcTotalLatency()
{
    // Total monitoring latency (what the user hears while recording):
    // USB mic ADC + capture period + half block (ring buffer mid-point) + output
    const float usbMicMs  = 2.0f;
    const float capMs     = captureMs.load();
    const float blkMs     = blockMs.load();
    const float outMs     = outputLatMs.load();
    const float total     = usbMicMs + capMs + blkMs * 0.5f + outMs;
    totalLatMs.store(total);

    // Report latency to DAW for Recording Offset compensation
    // This shifts recorded clips backwards so they land on the beat
    if (m_sampleRate > 0.0)
    {
        const int latencySamples = static_cast<int>(
            total / 1000.0f * static_cast<float>(m_sampleRate));
        setLatencySamples(latencySamples);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// prepareToPlay
// ─────────────────────────────────────────────────────────────────────────────
void MicInputProcessor::prepareToPlay(double sampleRate, int maxBlockSize)
{
    m_sampleRate = sampleRate;
    m_blockSize  = maxBlockSize;

    blockMs.store(static_cast<float>(maxBlockSize) / static_cast<float>(sampleRate) * 1000.0f);

    // Resize ring to 500ms at this sample rate
    const size_t ringFrames = static_cast<size_t>(sampleRate * 0.5);
    m_ring.resize(ringFrames);
    m_ring.reset();

    // Reopen capture with new ring (ring may have moved in memory)
    if (isCapturing.load())
    {
        closeCapture();
        openCapture();
    }

    recalcTotalLatency();
}

void MicInputProcessor::releaseResources()
{
    // Keep capture running so ring stays primed between stop/start.
    // Actual cleanup happens in destructor.
}

// ─────────────────────────────────────────────────────────────────────────────
// processBlock — THE HOT PATH
// Rules: no allocation, no locks, no GUI calls, no OS calls, no printf
// ─────────────────────────────────────────────────────────────────────────────
void MicInputProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = getTotalNumOutputChannels();

    buffer.clear();

    if (!isCapturing.load(std::memory_order_relaxed)) return;

    const size_t avail = m_ring.available();
    if (avail <= m_safetyFrames) return;   // not enough buffered yet

    const size_t consumable = avail - m_safetyFrames;
    const size_t toRead     = std::min(static_cast<size_t>(numSamples), consumable);
    if (toRead == 0) return;

    // Pre-allocated interleaved scratch (thread_local — one per thread, no alloc)
    if (s_interleavedBuf.size() < toRead * 2)
        s_interleavedBuf.resize(toRead * 2);

    const size_t got = m_ring.read(s_interleavedBuf.data(), toRead);
    if (got == 0)
    {
        underruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // De-interleave into JUCE output buffer
    float* outL = buffer.getWritePointer(0);
    float* outR = (numChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    for (size_t i = 0; i < got; ++i)
    {
        outL[i] = s_interleavedBuf[i * 2];
        if (outR) outR[i] = s_interleavedBuf[i * 2 + 1];
    }

    // Update level meters from capture thread's atomics (wait-free reads)
    levelL.store(m_capture.levelL(), std::memory_order_relaxed);
    levelR.store(m_capture.levelR(), std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// State persistence
// ─────────────────────────────────────────────────────────────────────────────
void MicInputProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto xml = apvts.copyStateAsXml();
    xml->setAttribute("deviceIndex", m_selectedDeviceIndex);
    xml->setAttribute("captureMode", m_captureMode);
    copyXmlToBinary(*xml, destData);
}

void MicInputProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return;

    if (xml->hasAttribute("captureMode"))
        m_captureMode = xml->getIntAttribute("captureMode", 0);

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (xml->hasAttribute("deviceIndex"))
    {
        int idx = xml->getIntAttribute("deviceIndex", -1);
        selectDevice(idx);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────────────────────
juce::AudioProcessorEditor* MicInputProcessor::createEditor()
{
    return new MicInputEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MicInputProcessor();
}
