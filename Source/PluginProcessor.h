#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "VoxEngine.h"

//==============================================================================
class VoxProcessor : public juce::AudioProcessor,
                     private juce::AsyncUpdater,
                     private juce::Timer,
                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    enum class ClipState { empty, waitingForBar, recording, loading, rendering, ready, failed };

    struct Rendered
    {
        juce::AudioBuffer<float> audio;
        int clipStart = 0;            // where the vocal starts (after the reverse swell)
        int clipEnd = 0;              // where the vocal ends (tail after it)
        std::vector<float> peaks;     // waveform overview
        double sampleRate = 44100.0;
    };

    VoxProcessor();
    ~VoxProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VonBerg Vox FX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // pads (message thread): momentary or latched, recorded as host automation
    void padDown (int fx);
    void padUp (int fx);
    bool isFxActive (int fx) const noexcept { return (activeMask.load() >> fx) & 1; }

    // sequencer
    juce::uint16 getSeqRow (int fx) const noexcept { return seq[(size_t) fx].load(); }
    void setSeqCell (int fx, int step, bool on);
    void setSeqRow (int fx, juce::uint16 bits);
    int getCurrentStep() const noexcept { return currentStep.load(); }
    static int numPresets();
    static juce::String presetName (int);
    void applyPreset (int);
    void randomizeSeq();

    // capture / print
    void toggleRecord();
    static bool isSupportedFile (const juce::String& path);
    void loadFile (const juce::File&);
    void clearClip();
    ClipState getClipState() const noexcept { return clipState.load(); }
    float getRecordSeconds() const noexcept { return recSeconds.load(); }
    juce::String getClipMessage() const { return clipMessage; }
    std::shared_ptr<const Rendered> getRendered() const;
    int getRenderVersion() const noexcept { return renderVersion.load(); }
    juce::File exportRendered();
    void setPreview (bool);
    bool isPreviewing() const noexcept { return previewing.load(); }
    double getPreviewPos() const noexcept { return previewPos.load(); }   // 0..1

    double getBpm() const noexcept { return hostBpm.load(); }
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float value);

    static juce::String onParamId (int fx) { return "on" + juce::String (fx); }

    juce::AudioProcessorValueTreeState apvts;

private:
    class RenderThread;
    class LoadThread;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    vfx::Settings currentSettings() const;
    vfx::Activity currentActivity (bool includePads) const;
    void requestRender();
    void startRender();

    vfx::Engine engine;
    std::array<std::atomic<float>*, vfx::numFx> onPtrs {};
    std::array<std::atomic<juce::uint16>, vfx::numFx> seq {};
    std::atomic<int> currentStep { -1 };
    std::atomic<int> activeMask { 0 };
    std::atomic<double> hostBpm { 120.0 };
    double freePpq = 0.0;
    double sampleRate = 44100.0;

    // capture
    std::atomic<ClipState> clipState { ClipState::empty };
    juce::AudioBuffer<float> recBuffer;          // preallocated in prepareToPlay
    std::atomic<int> recPos { 0 };
    std::atomic<float> recSeconds { 0.0f };
    std::atomic<bool> recStopRequested { false };
    juce::AudioBuffer<float> clip;               // dry vocal (message thread)
    juce::String clipName = "Vocal";
    juce::String clipMessage;

    juce::AudioFormatManager formats;
    std::unique_ptr<LoadThread> loader;
    std::unique_ptr<RenderThread> renderer;
    std::atomic<bool> renderDirty { false };
    int dirtyTicks = 0;

    juce::CriticalSection pendingLock;
    std::unique_ptr<juce::AudioBuffer<float>> pendingClip;
    std::shared_ptr<Rendered> pendingRender;
    juce::String pendingError;
    bool pendingLoadDone = false, pendingRenderDone = false;

    juce::SpinLock renderLock;
    std::shared_ptr<const Rendered> rendered;
    std::atomic<int> renderVersion { 0 };

    std::atomic<bool> previewing { false };
    std::atomic<double> previewPos { 0.0 };
    double previewSample = 0.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (VoxProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxProcessor)
};
