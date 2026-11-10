// End-to-end: pads/latch, sequencer presets, drop file -> render with swell+tail, REC, export, state, screenshots.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static juce::AudioBuffer<float> vocal (double sr, double seconds)
{
    juce::AudioBuffer<float> b (2, (int) (sr * seconds));
    double ph = 0;
    const int notes[] { 57, 60, 64, 62, 60, 57, 55, 57 };
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const int syl = (int) (i / (sr * 0.25)) % 8;
        const double t = std::fmod (i / sr, 0.25);
        ph += 2 * juce::MathConstants<double>::pi * 440.0 * std::pow (2.0, (notes[syl] - 69) / 12.0) / sr;
        const double env = juce::jmax (0.0, juce::jmin (1.0, t / 0.01) * juce::jmin (1.0, (0.25 - t) / 0.03));
        double s = 0; for (int h = 1; h < 8; ++h) s += std::sin (ph * h) / h;
        b.setSample (0, i, (float) (0.3 * env * s)); b.setSample (1, i, (float) (0.3 * env * s));
    }
    return b;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? argv[1] : "/tmp");
    int fails = 0;
    auto check = [&] (bool ok, const char* what, const juce::String& info = {})
    { std::printf ("%-34s %s %s\n", what, ok ? "OK " : "<-- CHECK", info.toRawUTF8()); if (! ok) ++fails; };

    const double sr = 48000.0;
    VoxProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto shot = [&] (const char* name)
    {
        pump (150);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile (name); f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat().writeImageToStream (img, fos);
    };
    shot ("vx_empty.png");

    juce::MidiBuffer midi;
    auto runBlocks = [&] (int blocks, const juce::AudioBuffer<float>* src = nullptr, int* srcPos = nullptr)
    {
        juce::AudioBuffer<float> b (2, 512);
        for (int k = 0; k < blocks; ++k)
        {
            b.clear();
            if (src != nullptr && srcPos != nullptr)
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
                    b.setSample (c, i, (*srcPos + i) < src->getNumSamples() ? src->getSample (c, *srcPos + i) : 0.0f);
            proc.processBlock (b, midi);
            if (srcPos != nullptr) *srcPos += 512;
        }
    };

    // pads: momentary
    proc.padDown (vfx::stutter);
    runBlocks (2);
    check (proc.isFxActive (vfx::stutter), "pad down -> stutter active");
    proc.padUp (vfx::stutter);
    runBlocks (2);
    check (! proc.isFxActive (vfx::stutter), "pad up -> stutter off");
    // latch
    proc.setParam ("latch", 1.0f);
    proc.padDown (vfx::gate); proc.padUp (vfx::gate);
    runBlocks (2);
    check (proc.isFxActive (vfx::gate), "latch keeps gate on");
    proc.padDown (vfx::gate); proc.padUp (vfx::gate);
    runBlocks (2);
    check (! proc.isFxActive (vfx::gate), "latch second press turns off");
    proc.setParam ("latch", 0.0f);

    // sequencer preset
    proc.applyPreset (1);   // trap stutter fill: steps 12-15
    check (proc.getSeqRow (vfx::stutter) == 0xF000 && proc.getParam ("seqOn") > 0.5f, "preset loads + enables seq");
    proc.randomizeSeq();
    int rows = 0; for (int f = 0; f < vfx::numFx; ++f) rows += proc.getSeqRow (f) != 0;
    check (rows >= 2, "random sequence", juce::String (rows) + " rows");

    // drop a vocal file -> render
    auto v = vocal (44100.0, 4.0);   // 4 s at 44.1k: also tests resampling to 48k
    {
        auto f = out.getChildFile ("vx_in.wav"); f.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (f.createOutputStream().release(), 44100.0, 2, 16, {}, 0));
        w->writeFromAudioSampleBuffer (v, 0, v.getNumSamples());
    }
    proc.applyPreset (6);   // reverse + stutter
    proc.loadFile (out.getChildFile ("vx_in.wav"));
    for (int i = 0; i < 200 && proc.getClipState() != VoxProcessor::ClipState::ready; ++i) pump (30);
    auto r = proc.getRendered();
    check (r != nullptr, "file -> rendered clip");
    if (r != nullptr)
    {
        const double spb = sr * 60.0 / 120.0;
        check (r->clipStart == (int) (4 * spb), "swell = 1 bar before vocal", juce::String (r->clipStart / sr, 2) + " s");
        check (std::abs ((r->clipEnd - r->clipStart) / sr - 4.0) < 0.01, "vocal resampled to 48k", juce::String ((r->clipEnd - r->clipStart) / sr, 3) + " s");
        check (r->audio.getNumSamples() >= r->clipEnd, "tail kept", juce::String ((r->audio.getNumSamples() - r->clipEnd) / sr, 2) + " s");
        check (r->audio.getMagnitude (0, r->clipStart) > 0.02f && r->audio.getMagnitude (0, r->audio.getNumSamples()) <= 1.0f, "swell audible, no clipping");
    }
    shot ("vx_rendered.png");

    // changing a setting re-renders
    const int ver = proc.getRenderVersion();
    proc.setParam ("swell", 0.0f);
    for (int i = 0; i < 100 && proc.getRenderVersion() == ver; ++i) pump (30);
    for (int i = 0; i < 100 && proc.getClipState() != VoxProcessor::ClipState::ready; ++i) pump (30);
    r = proc.getRendered();
    check (r != nullptr && r->clipStart == 0, "setting change re-renders (swell off)");

    // export
    auto wav = proc.exportRendered();
    check (wav.existsAsFile() && wav.getSize() > 10000, "export wav", wav.getFullPathName());

    // preview plays
    proc.setPreview (true);
    juce::AudioBuffer<float> b (2, 512);
    double e = 0;
    for (int k = 0; k < 50; ++k) { b.clear(); proc.processBlock (b, midi); e += b.getRMSLevel (0, 0, 512); }
    check (e > 0.5, "preview audible", juce::String (e, 2));
    shot ("vx_preview.png");
    proc.setPreview (false);

    // REC (no host transport -> starts immediately)
    proc.toggleRecord();
    int pos = 0;
    auto rec = vocal (sr, 2.0);
    runBlocks ((int) (2.0 * sr / 512), &rec, &pos);
    check (proc.getClipState() == VoxProcessor::ClipState::recording, "recording", juce::String (proc.getRecordSeconds(), 2) + " s");
    shot ("vx_recording.png");
    proc.toggleRecord();
    runBlocks (1);
    for (int i = 0; i < 200 && proc.getClipState() != VoxProcessor::ClipState::ready; ++i) pump (30);
    r = proc.getRendered();
    check (r != nullptr && std::abs ((r->clipEnd - r->clipStart) / sr - 2.0) < 0.05, "REC -> rendered clip",
           r != nullptr ? juce::String ((r->clipEnd - r->clipStart) / sr, 2) + " s" : juce::String());

    // state
    juce::MemoryBlock st;
    proc.getStateInformation (st);
    VoxProcessor p2;
    p2.setStateInformation (st.getData(), (int) st.getSize());
    bool same = true;
    for (int f = 0; f < vfx::numFx; ++f) same &= p2.getSeqRow (f) == proc.getSeqRow (f);
    check (same, "state keeps sequencer");

    // select a page with sliders for the screenshot
    ed.reset();
    std::printf (fails ? "FAILURES %d\n" : "ALL OK\n", fails);
    return fails;
}
