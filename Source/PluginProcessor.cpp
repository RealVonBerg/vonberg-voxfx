#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr double maxRecSeconds = 30.0;
static constexpr double maxFileSeconds = 60.0;
static constexpr int peakColumns = 1400;

//==============================================================================
static juce::File outputFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("VonBerg Vox FX");
    d.createDirectory();
    return d;
}

static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto os = f.createOutputStream();
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

//==============================================================================
class VoxProcessor::LoadThread : public juce::Thread
{
public:
    LoadThread (VoxProcessor& o, juce::File f, double targetRate)
        : juce::Thread ("VoxFX load"), owner (o), file (std::move (f)), rate (targetRate) {}
    ~LoadThread() override { stopThread (10000); }

    void run() override
    {
        std::unique_ptr<juce::AudioFormatReader> r (owner.formats.createReaderFor (file));
        if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0.0) return done (nullptr, "Can't read this file.");
        if ((double) r->lengthInSamples / r->sampleRate > maxFileSeconds) return done (nullptr, "File is too long (max 60 s).");

        juce::AudioBuffer<float> src ((int) juce::jmax (1u, r->numChannels), (int) r->lengthInSamples);
        r->read (&src, 0, (int) r->lengthInSamples, 0, true, true);
        if (threadShouldExit()) return;

        auto stereo = std::make_unique<juce::AudioBuffer<float>>();
        const double ratio = r->sampleRate / rate;
        const int outLen = (int) std::ceil (src.getNumSamples() / ratio);
        stereo->setSize (2, outLen);
        stereo->clear();
        for (int c = 0; c < 2; ++c)
        {
            const float* in = src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1));
            if (std::abs (ratio - 1.0) < 1.0e-9) stereo->copyFrom (c, 0, in, juce::jmin (outLen, src.getNumSamples()));
            else
            {
                juce::LagrangeInterpolator li;
                li.process (ratio, in, stereo->getWritePointer (c), outLen, src.getNumSamples(), 0);
            }
        }
        done (std::move (stereo), {});
    }

private:
    void done (std::unique_ptr<juce::AudioBuffer<float>> b, const juce::String& err)
    {
        {
            const juce::ScopedLock sl (owner.pendingLock);
            owner.pendingClip = std::move (b);
            owner.pendingError = err;
            owner.pendingLoadDone = true;
        }
        owner.triggerAsyncUpdate();
    }
    VoxProcessor& owner;
    juce::File file;
    double rate;
};

//==============================================================================
class VoxProcessor::RenderThread : public juce::Thread
{
public:
    RenderThread (VoxProcessor& o, juce::AudioBuffer<float> dry, double sr, double bpm, vfx::Settings s, vfx::Activity a,
                  double swellBeats, double tailBeats)
        : juce::Thread ("VoxFX render"), owner (o), clipAudio (std::move (dry)), rate (sr), tempo (bpm),
          settings (s), activity (a), swell (swellBeats), tail (tailBeats) {}
    ~RenderThread() override { stopThread (10000); }

    void run() override
    {
        const double spb = rate * 60.0 / tempo;
        const int swellLen = (int) (swell * spb), tailLen = (int) (tail * spb);
        const int n = clipAudio.getNumSamples();

        juce::AudioBuffer<float> wet (2, n + tailLen);
        wet.clear();
        for (int c = 0; c < 2; ++c) wet.copyFrom (c, 0, clipAudio, juce::jmin (c, clipAudio.getNumChannels() - 1), 0, n);

        vfx::Engine e;
        e.prepare (rate, 512);
        for (int pos = 0; pos < wet.getNumSamples(); pos += 512)
        {
            if (threadShouldExit()) return;
            const int num = juce::jmin (512, wet.getNumSamples() - pos);
            juce::AudioBuffer<float> blk (wet.getArrayOfWritePointers(), 2, pos, num);
            e.process (blk, num, pos / spb, tempo, activity, settings);
        }

        // trim silent tail
        int end = wet.getNumSamples();
        while (end > n && wet.getMagnitude (end - 1, 1) < 0.0003f) --end;

        auto r = std::make_shared<Rendered>();
        r->sampleRate = rate;
        r->clipStart = swellLen;
        r->clipEnd = swellLen + n;
        r->audio.setSize (2, swellLen + end);
        r->audio.clear();
        if (swellLen > 0)
        {
            auto sw = vfx::makeReverseSwell (clipAudio, rate, swellLen);
            for (int c = 0; c < 2; ++c) r->audio.copyFrom (c, 0, sw, c, 0, swellLen);
        }
        for (int c = 0; c < 2; ++c) r->audio.copyFrom (c, swellLen, wet, c, 0, end);

        // keep it out of clipping
        const float pk = r->audio.getMagnitude (0, r->audio.getNumSamples());
        if (pk > 0.99f) r->audio.applyGain (0.99f / pk);

        r->peaks.assign (peakColumns, 0.0f);
        const int total = r->audio.getNumSamples();
        for (int i = 0; i < total; ++i)
        {
            const size_t col = (size_t) ((juce::int64) i * peakColumns / juce::jmax (1, total));
            const float v = juce::jmax (std::abs (r->audio.getSample (0, i)), std::abs (r->audio.getSample (1, i)));
            r->peaks[col] = juce::jmax (r->peaks[col], v);
        }
        {
            const juce::ScopedLock sl (owner.pendingLock);
            owner.pendingRender = r;
            owner.pendingRenderDone = true;
        }
        owner.triggerAsyncUpdate();
    }

private:
    VoxProcessor& owner;
    juce::AudioBuffer<float> clipAudio;
    double rate, tempo;
    vfx::Settings settings;
    vfx::Activity activity;
    double swell, tail;
};

//==============================================================================
VoxProcessor::VoxProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOXFX", createLayout())
{
    formats.registerBasicFormats();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);
    for (auto& s : seq) s.store (0);
    for (int f = 0; f < vfx::numFx; ++f) onPtrs[(size_t) f] = apvts.getRawParameterValue (onParamId (f));
    startTimerHz (10);
}

VoxProcessor::~VoxProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    loader.reset();
    renderer.reset();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    static const char* names[] { "Stutter", "Reverse", "Tape Stop", "Glitch", "Pitch", "Filter", "Crush", "Gate", "Delay", "Reverb" };
    for (int i = 0; i < vfx::numFx; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { onParamId (i), 1 }, String (names[i]) + " On", false));

    auto choice = [&] (const char* id, const char* name, StringArray opts, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, opts, def)); };
    auto pct = [&] (const char* id, const char* name, float def, float maxV = 100.0f)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, maxV, 1.0f), def)); };

    choice ("stutDiv", "Stutter Rate", { "1/2", "1/4", "1/8", "1/16", "1/32", "Ramp" }, 3);
    choice ("revDiv", "Reverse Length", { "1/2", "1/4", "1/8", "1 Bar" }, 1);
    choice ("tapeLen", "Tape Stop Time", { "1/4", "1/2", "1 Bar", "2 Bars" }, 1);
    pct ("glitchAmt", "Glitch Amount", 60.0f);
    pct ("glitchSeed", "Glitch Variation", 7.0f, 99.0f);
    choice ("pitchSemi", "Pitch", { "-12", "-7", "-5", "+5", "+7", "+12" }, 0);
    choice ("filtMode", "Filter Mode", { "HP Build", "LP Drop", "Acid", "Phone" }, 0);
    pct ("filtRes", "Filter Resonance", 50.0f);
    pct ("crushAmt", "Crush Amount", 60.0f);
    choice ("gateRate", "Gate Rate", { "1/8", "1/16", "1/32", "1/8T", "1/16T" }, 1);
    pct ("gateDepth", "Gate Depth", 100.0f);
    choice ("dlyTime", "Delay Time", { "1/4", "1/8D", "1/8", "1/16" }, 2);
    pct ("dlyFb", "Delay Feedback", 55.0f, 90.0f);
    pct ("verbSize", "Reverb Size", 85.0f);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "verbFreeze", 1 }, "Reverb Freeze", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "seqOn", 1 }, "Sequencer On", false));
    choice ("seqRate", "Sequencer Length", { "1 Bar", "2 Bars", "4 Bars" }, 0);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "latch", 1 }, "Latch Pads", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "outGain", 1 }, "Output", NormalisableRange<float> (-12.0f, 6.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    choice ("swell", "Reverse Swell", { "Off", "1/2", "1 Bar", "2 Bars" }, 2);
    choice ("tail", "Tail", { "Off", "1 Bar", "2 Bars" }, 1);
    return l;
}

void VoxProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

bool VoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void VoxProcessor::prepareToPlay (double sr, int block)
{
    const bool rateChanged = std::abs (sr - sampleRate) > 1.0;
    sampleRate = sr > 0.0 ? sr : 44100.0;
    engine.prepare (sampleRate, block);
    recBuffer.setSize (2, (int) (sampleRate * maxRecSeconds) + 1);
    if (rateChanged && clip.getNumSamples() > 0) renderDirty.store (true);
}

//==============================================================================
vfx::Settings VoxProcessor::currentSettings() const
{
    vfx::Settings s;
    auto i = [this] (const char* id) { return (int) getParam (id); };
    s.stutDiv = i ("stutDiv");
    s.revDiv = i ("revDiv");
    s.tapeLen = i ("tapeLen");
    s.glitchAmt = getParam ("glitchAmt") / 100.0f;
    s.glitchSeed = i ("glitchSeed");
    s.pitchSemi = i ("pitchSemi");
    s.filtMode = i ("filtMode");
    s.filtRes = getParam ("filtRes") / 100.0f;
    s.crushAmt = getParam ("crushAmt") / 100.0f;
    s.gateRate = i ("gateRate");
    s.gateDepth = getParam ("gateDepth") / 100.0f;
    s.dlyTime = i ("dlyTime");
    s.dlyFb = getParam ("dlyFb") / 100.0f;
    s.verbSize = getParam ("verbSize") / 100.0f;
    s.verbFreeze = getParam ("verbFreeze") > 0.5f;
    s.outGain = juce::Decibels::decibelsToGain (getParam ("outGain"));
    return s;
}

vfx::Activity VoxProcessor::currentActivity (bool includePads) const
{
    vfx::Activity a;
    for (int f = 0; f < vfx::numFx; ++f)
    {
        a.on[(size_t) f] = includePads && onPtrs[(size_t) f]->load() > 0.5f;
        a.seq[(size_t) f] = seq[(size_t) f].load();
    }
    a.seqEnabled = getParam ("seqOn") > 0.5f;
    static const double stepBeats[] { 0.25, 0.5, 1.0 };
    a.stepBeats = stepBeats[juce::jlimit (0, 2, (int) getParam ("seqRate"))];
    return a;
}

//==============================================================================
void VoxProcessor::padDown (int fx)
{
    const auto id = onParamId (fx);
    if (getParam ("latch") > 0.5f) setParam (id, onPtrs[(size_t) fx]->load() > 0.5f ? 0.0f : 1.0f);
    else
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (1.0f); }
    }
}

void VoxProcessor::padUp (int fx)
{
    if (getParam ("latch") > 0.5f) return;
    if (auto* p = apvts.getParameter (onParamId (fx))) { p->setValueNotifyingHost (0.0f); p->endChangeGesture(); }
}

void VoxProcessor::setSeqCell (int fx, int step, bool on)
{
    auto v = seq[(size_t) fx].load();
    v = on ? (juce::uint16) (v | (1u << step)) : (juce::uint16) (v & ~(1u << step));
    seq[(size_t) fx].store (v);
    renderDirty.store (true);
    dirtyTicks = 0;
}

void VoxProcessor::setSeqRow (int fx, juce::uint16 bits)
{
    seq[(size_t) fx].store (bits);
    renderDirty.store (true);
    dirtyTicks = 0;
}

struct SeqPreset { const char* name; std::array<juce::uint16, vfx::numFx> rows; };
static const SeqPreset presets[]
{
    { "Empty",              { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "Trap Stutter Fill",  { 0xF000, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "Techno Gate",        { 0, 0, 0, 0, 0, 0, 0, 0xFFFF, 0, 0 } },
    { "Glitch Chop",        { 0, 0x00C0, 0, 0xFF00, 0, 0, 0, 0, 0, 0 } },
    { "Vocal Throw",        { 0, 0, 0, 0, 0, 0, 0, 0, 0xC000, 0x8000 } },
    { "Tape Stop Ending",   { 0, 0, 0xFF00, 0, 0, 0, 0, 0, 0, 0 } },
    { "Reverse + Stutter",  { 0xF000, 0x0F00, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "Pitch Drop",         { 0, 0, 0, 0, 0xF0F0, 0, 0, 0, 0, 0 } },
    { "Crush Hits",         { 0, 0, 0, 0, 0, 0, 0x8888, 0, 0, 0 } },
    { "Build (HP + Stutter)", { 0xFF00, 0, 0, 0, 0, 0xFFFF, 0, 0, 0, 0 } },
    { "Acid Chop",          { 0, 0, 0, 0, 0, 0xFFFF, 0, 0x0F0F, 0, 0 } },
};

int VoxProcessor::numPresets() { return (int) (sizeof (presets) / sizeof (presets[0])); }
juce::String VoxProcessor::presetName (int i) { return presets[juce::jlimit (0, numPresets() - 1, i)].name; }

void VoxProcessor::applyPreset (int i)
{
    const auto& p = presets[juce::jlimit (0, numPresets() - 1, i)];
    for (int f = 0; f < vfx::numFx; ++f) setSeqRow (f, p.rows[(size_t) f]);
    if (juce::String (p.name) == "Acid Chop") setParam ("filtMode", 2.0f);
    else if (juce::String (p.name).startsWith ("Build")) setParam ("filtMode", 0.0f);
    if (i > 0) setParam ("seqOn", 1.0f);
}

void VoxProcessor::randomizeSeq()
{
    juce::Random r;
    for (int f = 0; f < vfx::numFx; ++f) seq[(size_t) f].store (0);
    const int picks = 2 + r.nextInt (2);
    const int pool[] { vfx::stutter, vfx::reverse, vfx::tapeStop, vfx::glitch, vfx::pitch, vfx::filter, vfx::crush, vfx::gate, vfx::delay, vfx::reverb };
    std::vector<int> used;
    for (int k = 0; k < picks; ++k)
    {
        int fx;
        do fx = pool[r.nextInt (vfx::numFx)]; while (std::find (used.begin(), used.end(), fx) != used.end());
        used.push_back (fx);
        juce::uint16 bits = 0;
        if (fx == vfx::gate || (fx == vfx::filter && r.nextBool()))
            bits = r.nextBool() ? 0xFFFF : 0xFF00;
        else
        {
            const int starts[] { 4, 6, 8, 10, 12, 12, 14 };
            const int start = starts[r.nextInt (7)];
            const int len = juce::jmin (16 - start, r.nextBool() ? 2 : 4);
            for (int s = start; s < start + len; ++s) bits |= (juce::uint16) (1u << s);
        }
        seq[(size_t) fx].store (bits);
    }
    renderDirty.store (true);
    dirtyTicks = 0;
    setParam ("seqOn", 1.0f);
}

//==============================================================================
void VoxProcessor::toggleRecord()
{
    const auto st = clipState.load();
    if (st == ClipState::recording || st == ClipState::waitingForBar)
    {
        recStopRequested.store (true);
        return;
    }
    previewing.store (false);
    recPos.store (0);
    recSeconds.store (0.0f);
    recStopRequested.store (false);
    clipMessage = {};
    clipState.store (ClipState::waitingForBar);
}

bool VoxProcessor::isSupportedFile (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    for (auto* e : { ".wav", ".wave", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".wma", ".aac" })
        if (ext == e) return true;
    return false;
}

void VoxProcessor::loadFile (const juce::File& f)
{
    previewing.store (false);
    renderer.reset();
    loader.reset();
    clipName = f.getFileNameWithoutExtension();
    apvts.state.setProperty ("clipFile", f.getFullPathName(), nullptr);
    clipMessage = {};
    clipState.store (ClipState::loading);
    loader = std::make_unique<LoadThread> (*this, f, sampleRate);
    loader->startThread();
}

void VoxProcessor::clearClip()
{
    previewing.store (false);
    loader.reset();
    renderer.reset();
    clip.setSize (0, 0);
    apvts.state.setProperty ("clipFile", "", nullptr);
    {
        const juce::SpinLock::ScopedLockType sl (renderLock);
        rendered.reset();
    }
    ++renderVersion;
    clipState.store (ClipState::empty);
}

void VoxProcessor::startRender()
{
    if (clip.getNumSamples() == 0) return;
    renderer.reset();
    static const double swellBeats[] { 0.0, 2.0, 4.0, 8.0 };
    static const double tailBeats[] { 0.0, 4.0, 8.0 };
    renderer = std::make_unique<RenderThread> (*this, clip, sampleRate, hostBpm.load(), currentSettings(), currentActivity (true),
                                               swellBeats[juce::jlimit (0, 3, (int) getParam ("swell"))],
                                               tailBeats[juce::jlimit (0, 2, (int) getParam ("tail"))]);
    clipState.store (ClipState::rendering);
    renderer->startThread();
}

void VoxProcessor::requestRender()
{
    renderDirty.store (true);
    dirtyTicks = 0;
}

void VoxProcessor::parameterChanged (const juce::String& id, float)
{
    // momentary pads don't re-render the printed clip, everything else does
    if (id.startsWith ("on") && getParam ("latch") < 0.5f) return;
    if (id == "latch" || id == "outGain") return;
    renderDirty.store (true);
    dirtyTicks = 0;
}

void VoxProcessor::timerCallback()
{
    if (renderDirty.load() && clip.getNumSamples() > 0)
    {
        const auto st = clipState.load();
        if (st != ClipState::recording && st != ClipState::waitingForBar && st != ClipState::loading && ++dirtyTicks >= 3)
        {
            renderDirty.store (false);
            dirtyTicks = 0;
            startRender();
        }
    }
}

void VoxProcessor::handleAsyncUpdate()
{
    // recording finished on the audio thread
    if (clipState.load() == ClipState::rendering && recStopRequested.exchange (false))
    {
        const int n = recPos.load();
        if (n < (int) (sampleRate * 0.1))
        {
            clipMessage = "Recording was empty.";
            clipState.store (clip.getNumSamples() > 0 ? ClipState::ready : ClipState::empty);
        }
        else
        {
            clip.setSize (2, n);
            for (int c = 0; c < 2; ++c) clip.copyFrom (c, 0, recBuffer, c, 0, n);
            clipName = "Vocal " + juce::Time::getCurrentTime().formatted ("%H-%M-%S");
            auto f = outputFolder().getChildFile (clipName + " (dry).wav");
            if (writeWav (f, clip, sampleRate)) apvts.state.setProperty ("clipFile", f.getFullPathName(), nullptr);
            startRender();
        }
    }

    bool loadDone = false, renderDone = false;
    std::unique_ptr<juce::AudioBuffer<float>> newClip;
    std::shared_ptr<Rendered> newRender;
    juce::String err;
    {
        const juce::ScopedLock sl (pendingLock);
        loadDone = pendingLoadDone; pendingLoadDone = false;
        renderDone = pendingRenderDone; pendingRenderDone = false;
        newClip = std::move (pendingClip);
        newRender = std::move (pendingRender);
        err = pendingError; pendingError = {};
    }
    if (loadDone)
    {
        loader.reset();
        if (newClip == nullptr)
        {
            clipMessage = err;
            clipState.store (ClipState::failed);
        }
        else
        {
            clip = std::move (*newClip);
            startRender();
        }
    }
    if (renderDone && newRender != nullptr)
    {
        renderer.reset();
        {
            const juce::SpinLock::ScopedLockType sl (renderLock);
            rendered = newRender;
        }
        ++renderVersion;
        if (! renderDirty.load()) clipState.store (ClipState::ready);
        else clipState.store (ClipState::ready);
    }
}

std::shared_ptr<const VoxProcessor::Rendered> VoxProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType sl (renderLock);
    return rendered;
}

juce::File VoxProcessor::exportRendered()
{
    auto r = getRendered();
    if (r == nullptr) return {};
    auto f = outputFolder().getChildFile (juce::File::createLegalFileName (clipName + " VoxFX.wav"));
    return writeWav (f, r->audio, r->sampleRate) ? f : juce::File();
}

void VoxProcessor::setPreview (bool p)
{
    if (p) previewSample = 0.0;
    previewing.store (p && getRendered() != nullptr);
}

//==============================================================================
void VoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    double ppq = freePpq;
    bool playingHost = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 999.0)
            {
                if (std::abs (*b - hostBpm.load()) > 0.01 && clip.getNumSamples() > 0) renderDirty.store (true);
                hostBpm.store (*b);
            }
            if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) { ppq = *q; playingHost = true; }
        }
    const double bpm = hostBpm.load();
    const double beatsPerSample = bpm / 60.0 / sampleRate;

    // ---- capture (dry input) ----------------------------------------------------------
    auto st = clipState.load();
    if (st == ClipState::waitingForBar)
    {
        int startAt = -1;
        if (! playingHost) startAt = 0;
        else
        {
            const double nextBar = std::ceil (ppq / 4.0 - 1.0e-9) * 4.0;
            const int off = (int) std::round ((nextBar - ppq) / beatsPerSample);
            if (off < n) startAt = juce::jmax (0, off);
        }
        if (recStopRequested.load()) { recStopRequested.store (false); clipState.store (clip.getNumSamples() > 0 ? ClipState::ready : ClipState::empty); }
        else if (startAt >= 0)
        {
            clipState.store (ClipState::recording);
            st = ClipState::recording;
            const int num = n - startAt;
            for (int c = 0; c < 2; ++c) recBuffer.copyFrom (c, 0, buffer, juce::jmin (c, buffer.getNumChannels() - 1), startAt, num);
            recPos.store (num);
        }
    }
    else if (st == ClipState::recording)
    {
        int pos = recPos.load();
        const int num = juce::jmin (n, recBuffer.getNumSamples() - pos);
        if (num > 0)
            for (int c = 0; c < 2; ++c) recBuffer.copyFrom (c, pos, buffer, juce::jmin (c, buffer.getNumChannels() - 1), 0, num);
        pos += juce::jmax (0, num);
        recPos.store (pos);
        recSeconds.store ((float) (pos / sampleRate));
        if (recStopRequested.load() || pos >= recBuffer.getNumSamples())
        {
            recStopRequested.store (true);
            clipState.store (ClipState::rendering);
            triggerAsyncUpdate();
        }
    }

    // ---- live FX ------------------------------------------------------------------------
    const auto act = currentActivity (true);
    engine.process (buffer, n, ppq, bpm, act, currentSettings());
    int mask = 0;
    for (int f = 0; f < vfx::numFx; ++f) if (act.isActive (f, ppq)) mask |= 1 << f;
    activeMask.store (mask);
    currentStep.store (act.seqEnabled ? (((int) std::floor (ppq / act.stepBeats)) & 15) : -1);
    if (! playingHost) freePpq = ppq + n * beatsPerSample;

    // ---- preview of the printed clip --------------------------------------------------
    if (previewing.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (renderLock);
        if (lock.isLocked() && rendered != nullptr)
        {
            const auto& a = rendered->audio;
            const double step = rendered->sampleRate / sampleRate;
            for (int i = 0; i < n; ++i)
            {
                const int idx = (int) previewSample;
                if (idx >= a.getNumSamples()) { previewing.store (false); break; }
                for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.addSample (c, i, a.getSample (juce::jmin (c, 1), idx));
                previewSample += step;
            }
            previewPos.store (previewSample / juce::jmax (1, a.getNumSamples()));
        }
    }
}

//==============================================================================
void VoxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::StringArray rows;
    for (auto& s : seq) rows.add (juce::String (s.load()));
    state.setProperty ("seq", rows.joinIntoString (","), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void VoxProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);
    juce::StringArray rows;
    rows.addTokens (state.getProperty ("seq").toString(), ",", "");
    for (int f = 0; f < vfx::numFx && f < rows.size(); ++f) seq[(size_t) f].store ((juce::uint16) rows[f].getIntValue());

    const auto path = state.getProperty ("clipFile").toString();
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<VoxProcessor> (this), path]
        {
            if (auto* p = safe.get())
                if (juce::File (path).existsAsFile()) p->loadFile (juce::File (path));
        });
    }
}

juce::AudioProcessorEditor* VoxProcessor::createEditor() { return new VoxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxProcessor(); }
