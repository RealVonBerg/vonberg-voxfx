// Offline DSP checks for the Vox FX engine (not part of the plugin).
#include "../Source/VoxEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static const double sr = 48000.0, bpm = 120.0;           // 1 beat = 0.5 s = 24000 samples
static const int spb = 24000;

// "vocal": syllables with changing pitch, 2 bars
static juce::AudioBuffer<float> makeVocal (int samples)
{
    juce::AudioBuffer<float> b (2, samples);
    b.clear();
    double ph = 0;
    const int notes[] { 57, 60, 64, 62, 60, 57, 55, 57 };
    for (int i = 0; i < samples; ++i)
    {
        const int syl = (i / (spb / 2)) % 8;                 // 8th-note syllables
        const double t = (i % (spb / 2)) / sr;
        const double f = 440.0 * std::pow (2.0, (notes[syl] - 69) / 12.0);
        ph += 2 * juce::MathConstants<double>::pi * f / sr;
        const double env = juce::jmin (1.0, t / 0.01) * juce::jmin (1.0, (0.25 - t) / 0.03);
        double s = 0; for (int h = 1; h < 8; ++h) s += std::sin (ph * h) / h;
        const float v = (float) (0.3 * juce::jmax (0.0, env) * s);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

struct Result { juce::AudioBuffer<float> out; float maxJump = 0, peak = 0; bool finite = true; };

static Result run (int fx, const vfx::Settings& s, int samples, double onFrom, double onTo, juce::AudioBuffer<float> in)
{
    vfx::Engine e;
    e.prepare (sr, 512);
    Result r;
    r.out.setSize (2, samples);
    r.out.clear();
    for (int c = 0; c < 2; ++c) r.out.copyFrom (c, 0, in, c, 0, juce::jmin (samples, in.getNumSamples()));
    for (int pos = 0; pos < samples; pos += 512)
    {
        const int n = juce::jmin (512, samples - pos);
        juce::AudioBuffer<float> blk (r.out.getArrayOfWritePointers(), 2, pos, n);
        vfx::Activity a;
        const double ppq = pos / (double) spb;
        a.on[(size_t) fx] = ppq >= onFrom && ppq < onTo;
        e.process (blk, n, ppq, bpm, a, s);
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < samples; ++i)
        {
            const float v = r.out.getSample (c, i);
            if (! std::isfinite (v)) r.finite = false;
            r.peak = juce::jmax (r.peak, std::abs (v));
            if (i > 0) r.maxJump = juce::jmax (r.maxJump, std::abs (v - r.out.getSample (c, i - 1)));
        }
    return r;
}

static double rms (const juce::AudioBuffer<float>& b, int a, int e)
{
    double s = 0; for (int i = a; i < e; ++i) s += b.getSample (0, i) * b.getSample (0, i);
    return std::sqrt (s / juce::jmax (1, e - a));
}

static double corr (const juce::AudioBuffer<float>& b, int a1, const juce::AudioBuffer<float>& c, int a2, int len, bool reversed = false)
{
    double xy = 0, xx = 0, yy = 0;
    for (int i = 0; i < len; ++i)
    {
        const double x = b.getSample (0, a1 + i);
        const double y = reversed ? c.getSample (0, a2 + len - 1 - i) : c.getSample (0, a2 + i);
        xy += x * y; xx += x * x; yy += y * y;
    }
    return xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 0.0;
}

static int zeroCrossings (const juce::AudioBuffer<float>& b, int a, int e)
{
    int z = 0; for (int i = a + 1; i < e; ++i) z += (b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0);
    return z;
}

int main()
{
    const int len = spb * 8;   // 2 bars
    auto vox = makeVocal (len);
    float dryJump = 0;
    for (int i = 1; i < len; ++i) dryJump = juce::jmax (dryJump, std::abs (vox.getSample (0, i) - vox.getSample (0, i - 1)));
    int fails = 0;
    auto check = [&] (bool ok, const char* what, const juce::String& info)
    {
        std::printf ("%-28s %s  %s\n", what, ok ? "OK " : "<-- CHECK", info.toRawUTF8());
        if (! ok) ++fails;
    };

    vfx::Settings s;
    // general safety for every FX
    for (int fx = 0; fx < vfx::numFx; ++fx)
    {
        auto r = run (fx, s, len, 1.0, 5.0, vox);
        check (r.finite && r.peak < 1.2f && (fx == vfx::crush || r.maxJump < 0.2f), vfx::fxName (fx),
               "peak " + juce::String (r.peak, 2) + "  max step " + juce::String (r.maxJump, 3) + " (dry " + juce::String (dryJump, 3) + ")");
    }

    // stutter 1/16 engaged on beat 2: every 1/16 slice repeats the first one
    {
        s = {}; s.stutDiv = 3;
        auto r = run (vfx::stutter, s, len, 2.0, 4.0, vox);
        const int L = spb / 4, st = 2 * spb;
        check (corr (r.out, st + L, r.out, st, L) > 0.95 && corr (r.out, st + 5 * L, vox, st, L) > 0.95, "stutter repeats slice",
               "corr " + juce::String (corr (r.out, st + L, r.out, st, L), 3));
    }
    // reverse 1/4 engaged on beat 2: plays beat 1 backwards
    {
        s = {}; s.revDiv = 1;
        auto r = run (vfx::reverse, s, len, 2.0, 3.0, vox);
        const double c = corr (r.out, 2 * spb + 2000, vox, spb + 2000, spb - 4000, true);
        juce::ignoreUnused (c);
        // compare middle part: out[2spb + j] == vox[2spb - 1 - j]
        double xy = 0, xx = 0, yy = 0;
        for (int j = 2000; j < spb - 2000; ++j) { const double a = r.out.getSample (0, 2 * spb + j), b = vox.getSample (0, 2 * spb - 1 - j); xy += a * b; xx += a * a; yy += b * b; }
        const double cc = xy / std::sqrt (xx * yy);
        check (cc > 0.95, "reverse plays last beat", "corr " + juce::String (cc, 3));
    }
    // tape stop 1/2 note: energy fades out and pitch drops
    {
        s = {}; s.tapeLen = 1;
        auto r = run (vfx::tapeStop, s, len, 2.0, 6.0, vox);
        const double early = rms (r.out, 2 * spb, 2 * spb + spb / 2), late = rms (r.out, 4 * spb + 2000, 6 * spb);
        const int z1 = zeroCrossings (r.out, 2 * spb, 2 * spb + spb / 4), z2 = zeroCrossings (r.out, 3 * spb, 3 * spb + spb / 4);
        check (late < 0.01 * early && z2 < z1, "tape stop slows to silence", "zc " + juce::String (z1) + " -> " + juce::String (z2) + ", rms after " + juce::String (late, 5));
    }
    // pitch -12: zero crossings roughly halve
    {
        s = {}; s.pitchSemi = 0;
        auto r = run (vfx::pitch, s, len, 0.0, 8.0, vox);
        const auto pure = makeVocal (len);
        // use a steady sine so zero crossings are meaningful
        juce::AudioBuffer<float> sine (2, len);
        for (int i = 0; i < len; ++i) { const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440.0 * i / sr); sine.setSample (0, i, v); sine.setSample (1, i, v); }
        auto rs = run (vfx::pitch, s, len, 0.0, 8.0, sine);
        const int zin = zeroCrossings (sine, spb, 2 * spb), zout = zeroCrossings (rs.out, spb, 2 * spb);
        check (std::abs ((double) zout / zin - 0.5) < 0.06, "pitch -12 = octave down", juce::String (zin) + " -> " + juce::String (zout) + " zero crossings");
        juce::ignoreUnused (pure, r);
    }
    // gate 1/16 full depth: every second 1/32 is silent
    {
        s = {}; s.gateRate = 1; s.gateDepth = 1.0f;
        auto r = run (vfx::gate, s, len, 0.0, 8.0, vox);
        const int L = spb / 8;
        const double on = rms (r.out, spb + 400, spb + L - 400), off = rms (r.out, spb + L + 400, spb + 2 * L - 400);
        check (off < 0.02 * on, "gate chops on the grid", "on " + juce::String (on, 3) + " off " + juce::String (off, 5));
    }
    // delay throw on beat 1 only: echoes keep going after release
    {
        s = {}; s.dlyTime = 2; s.dlyFb = 0.5f;
        juce::AudioBuffer<float> one (2, len); one.clear();
        for (int c = 0; c < 2; ++c) one.copyFrom (c, 0, vox, c, 0, spb);   // signal only during beat 1
        auto r = run (vfx::delay, s, len, 0.0, 1.0, one);
        check (rms (r.out, 2 * spb, 3 * spb) > 0.01, "delay tail after release", "rms beat 3 " + juce::String (rms (r.out, 2 * spb, 3 * spb), 4));
    }
    // reverb throw: tail after the signal stops
    {
        s = {};
        juce::AudioBuffer<float> one (2, len); one.clear();
        for (int c = 0; c < 2; ++c) one.copyFrom (c, 0, vox, c, 0, spb);
        auto r = run (vfx::reverb, s, len, 0.0, 1.0, one);
        check (rms (r.out, spb + spb / 2, 2 * spb) > 0.005, "reverb tail after release", "rms " + juce::String (rms (r.out, spb + spb / 2, 2 * spb), 4));
    }
    // glitch changes the audio, deterministically
    {
        s = {}; s.glitchAmt = 0.8f;
        auto r1 = run (vfx::glitch, s, len, 0.0, 8.0, vox);
        auto r2 = run (vfx::glitch, s, len, 0.0, 8.0, vox);
        double diff = 0, same = 0;
        for (int i = 0; i < len; ++i) { diff += std::abs (r1.out.getSample (0, i) - vox.getSample (0, i)); same += std::abs (r1.out.getSample (0, i) - r2.out.getSample (0, i)); }
        check (diff > 100.0 && same < 1e-3, "glitch: different + repeatable", "diff " + juce::String (diff, 1));
    }
    // sequencer: stutter on step 4..7 of each bar via the seq bits
    {
        vfx::Engine e; e.prepare (sr, 512);
        auto buf = vox;
        vfx::Activity a; a.seqEnabled = true; a.seq[vfx::stutter] = 0x00F0; a.stepBeats = 0.25;
        s = {}; s.stutDiv = 3;
        for (int pos = 0; pos < len; pos += 512) { juce::AudioBuffer<float> blk (buf.getArrayOfWritePointers(), 2, pos, 512); e.process (blk, 512, pos / (double) spb, bpm, a, s); }
        const int L = spb / 4;
        check (corr (buf, 5 * L, buf, 4 * L, L) > 0.95 && corr (buf, 9 * L, vox, 9 * L, L) > 0.95, "sequencer drives FX", "");
    }
    // reverse swell
    {
        auto sw = vfx::makeReverseSwell (vox, sr, spb * 2);
        const double first = rms (sw, 0, spb / 2), last = rms (sw, spb * 2 - spb / 2, spb * 2);
        check (last > 4.0 * first && sw.getMagnitude (0, sw.getNumSamples()) < 1.0f, "reverse swell grows into clip",
               juce::String (first, 4) + " -> " + juce::String (last, 4));
    }

    // write a demo of every FX for listening
    {
        juce::WavAudioFormat wav;
        for (int fx = 0; fx < vfx::numFx; ++fx)
        {
            s = {};
            auto r = run (fx, s, len, 2.0, 6.0, vox);
            juce::File f ("/tmp/vox_" + juce::String (vfx::fxName (fx)).replace (" ", "_") + ".wav");
            f.deleteFile();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), sr, 2, 16, {}, 0));
            if (w) w->writeFromAudioSampleBuffer (r.out, 0, len);
        }
    }
    std::printf (fails ? "FAILURES %d\n" : "ALL OK\n", fails);
    return fails;
}
