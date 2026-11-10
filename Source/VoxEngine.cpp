#include "VoxEngine.h"
#include <cmath>

namespace vfx
{
const char* fxName (int fx)
{
    static const char* names[] { "STUTTER", "REVERSE", "TAPE STOP", "GLITCH", "PITCH", "FILTER", "CRUSH", "GATE", "DELAY", "REVERB" };
    return names[juce::jlimit (0, numFx - 1, fx)];
}

static constexpr double pi = juce::MathConstants<double>::pi;
static constexpr int chunk = 1024;

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    histLen = (juce::int64) (sr * 12.0);
    for (auto& h : hist) h.assign ((size_t) histLen, 0.0f);
    int pl = 8192;
    while (pl < (int) (sr * 0.12)) pl *= 2;
    for (auto& b : pbuf) b.assign ((size_t) pl, 0.0f);
    for (auto& b : dbuf) b.assign ((size_t) (sr * 4.5), 0.0f);
    sendL.assign (chunk, 0.0f);
    sendR.assign (chunk, 0.0f);
    verb.setSampleRate (sr);
    verbParams.roomSize = 0.85f;
    verbParams.damping = 0.35f;
    verbParams.wetLevel = 0.3f;    // juce::Reverb scales wet by 3x internally
    verbParams.dryLevel = 0.0f;
    verbParams.width = 1.0f;
    verbParams.freezeMode = 0.0f;
    verb.setParameters (verbParams);
    reset();
}

void Engine::reset()
{
    for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
    for (auto& b : pbuf) std::fill (b.begin(), b.end(), 0.0f);
    for (auto& b : dbuf) std::fill (b.begin(), b.end(), 0.0f);
    now = histLen;   // start one buffer in so reads of "the past" are valid (silence)
    prevActive.fill (false);
    levels.fill (0.0f);
    manipMix = pitchMix = filterMix = crushMix = dSend = vSend = 0.0f;
    gateGain = 1.0f;
    glitchStep = -1;
    pPos = 0; pPhase = 0.0; dPos = 0;
    for (int c = 0; c < 2; ++c) { ic1[c] = ic2[c] = crushHold[c] = dlyHp[c] = lastManip[c] = 0.0f; }
    verb.reset();
}

float Engine::readHist (int ch, double pos) const
{
    const double fl = std::floor (pos);
    const float frac = (float) (pos - fl);
    juce::int64 i0 = (juce::int64) fl;
    i0 = ((i0 % histLen) + histLen) % histLen;
    const juce::int64 i1 = (i0 + 1) % histLen;
    const auto& h = hist[ch];
    return h[(size_t) i0] + frac * (h[(size_t) i1] - h[(size_t) i0]);
}

static double divBeats (int idx)
{
    static const double d[] { 2.0, 1.0, 0.5, 0.25, 0.125 };
    return d[juce::jlimit (0, 4, idx)];
}

static float seam (double off, double L, double fade)
{
    const double f = juce::jmin (fade, L * 0.25);
    if (f <= 1.0) return 1.0f;
    return (float) juce::jmin (1.0, off / f, (L - off) / f);
}

static juce::uint32 hash32 (juce::uint32 x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

void Engine::process (juce::AudioBuffer<float>& buffer, int numSamples, double ppq, double bpm,
                      const Activity& act, const Settings& s)
{
    if (histLen <= 0) return;
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const double spb = sr * 60.0 / bpm;          // samples per beat
    const int chans = juce::jmin (2, buffer.getNumChannels());
    if (chans <= 0) return;
    const float smooth = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
    const float gateSmooth = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));
    const double fade = 0.003 * sr;
    const int pLen = (int) pbuf[0].size();
    const int dLen = (int) dbuf[0].size();

    static const double tapeBeats[] { 1.0, 2.0, 4.0, 8.0 };
    static const double revBeats[]  { 2.0, 1.0, 0.5, 4.0 };
    static const double gateBeats[] { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    static const double dlyBeats[]  { 1.0, 0.75, 0.5, 0.25 };
    static const int pitchSemis[]   { -12, -7, -5, 5, 7, 12 };

    for (int base = 0; base < numSamples; base += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - base);
        bool freezeNow = false;

        for (int k = 0; k < n; ++k)
        {
            const int i = base + k;
            const double p = ppq + (double) i / spb;
            float x[2];
            for (int c = 0; c < 2; ++c)
            {
                x[c] = buffer.getSample (juce::jmin (c, chans - 1), i);
                hist[c][(size_t) (now % histLen)] = x[c];
            }

            bool a[numFx];
            for (int f = 0; f < numFx; ++f)
            {
                a[f] = act.isActive (f, p);
                if (a[f] && ! prevActive[(size_t) f])
                {
                    engagePpq[(size_t) f] = p;
                    engageSample[(size_t) f] = now;
                    if (f == tapeStop) { tapeRead = (double) now; tapeSpeed = 1.0; }
                    if (f == filter) { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
                }
                prevActive[(size_t) f] = a[f];
                levels[(size_t) f] = a[f] ? 1.0f : 0.0f;
            }

            // ---- buffer manipulators (tape > reverse > stutter > glitch) -------------
            auto aligned = [&] (int f, double div)
            {
                const double e = engagePpq[(size_t) f];
                const double grid = std::floor (e / div + 1.0e-9) * div;
                return (double) engageSample[(size_t) f] - (e - grid) * spb;
            };

            bool manip = false;
            float m[2] { x[0], x[1] };
            const double nowD = (double) now;

            if (a[tapeStop] || (manipMix > 0.001f && tapeSpeed < 1.0 && ! a[reverse] && ! a[stutter] && ! a[glitch] && prevActive[tapeStop]))
            {
                const double D = tapeBeats[juce::jlimit (0, 3, s.tapeLen)] * spb;
                tapeSpeed = juce::jmax (0.0, tapeSpeed - 1.0 / D);
                tapeRead += tapeSpeed;
                const float g = (float) juce::jmin (1.0, tapeSpeed / 0.04);
                for (int c = 0; c < 2; ++c) m[c] = readHist (c, juce::jmin (tapeRead, nowD)) * g;
                manip = a[tapeStop];
            }
            else if (a[reverse])
            {
                const double div = revBeats[juce::jlimit (0, 3, s.revDiv)];
                const double L = div * spb;
                const double start = aligned (reverse, div);
                const double off = std::fmod (juce::jmax (0.0, nowD - start), L);
                const float w = seam (off, L, fade);
                for (int c = 0; c < 2; ++c) m[c] = readHist (c, start - 1.0 - off) * w;
                manip = true;
            }
            else if (a[stutter])
            {
                double div;
                if (s.stutDiv >= 5)   // RAMP: 1/4 -> 1/8 -> 1/16 -> 1/32 -> 1/64, one step per beat
                {
                    const double el = p - engagePpq[stutter];
                    div = el < 1.0 ? 1.0 : el < 2.0 ? 0.5 : el < 3.0 ? 0.25 : el < 3.5 ? 0.125 : 0.0625;
                }
                else div = divBeats (s.stutDiv);
                const double L = juce::jmax (32.0, div * spb);
                const double start = aligned (stutter, s.stutDiv >= 5 ? 1.0 : div);
                const double off = std::fmod (juce::jmax (0.0, nowD - start), L);
                const float w = seam (off, L, fade);
                for (int c = 0; c < 2; ++c) m[c] = readHist (c, start + off) * w;
                manip = true;
            }
            else if (a[glitch])
            {
                const juce::int64 step = (juce::int64) std::floor (p / 0.25);
                if (step != glitchStep)
                {
                    glitchStep = step;
                    glitchStart = now;
                    const juce::uint32 h = hash32 ((juce::uint32) step * 2654435761u ^ (juce::uint32) (s.glitchSeed * 97531 + 13));
                    const float r = (float) (h & 0xffff) / 65535.0f;
                    glitchOp = r > s.glitchAmt ? 0 : 1 + (int) ((h >> 16) % 5u);
                }
                const double L16 = 0.25 * spb;
                const double t = nowD - (double) glitchStart;
                switch (glitchOp)
                {
                    case 1: { const double L = L16 * 0.5; const double off = std::fmod (t, L);
                              for (int c = 0; c < 2; ++c) m[c] = readHist (c, (double) glitchStart + off) * seam (off, L, fade * 0.5); break; }
                    case 2: { const double off = std::fmod (t, L16);
                              for (int c = 0; c < 2; ++c) m[c] = readHist (c, (double) glitchStart - L16 + off) * seam (off, L16, fade * 0.5); break; }
                    case 3: { const double off = std::fmod (t, L16);
                              for (int c = 0; c < 2; ++c) m[c] = readHist (c, (double) glitchStart - 1.0 - off) * seam (off, L16, fade * 0.5); break; }
                    case 4: for (int c = 0; c < 2; ++c) m[c] = readHist (c, (double) glitchStart + t * 0.5) * seam (t, L16, fade * 0.5); break;
                    case 5: m[0] = m[1] = 0.0f; break;
                    default: break;
                }
                manip = glitchOp != 0;
            }

            if (manip) { lastManip[0] = m[0]; lastManip[1] = m[1]; }
            manipMix += ((manip ? 1.0f : 0.0f) - manipMix) * smooth;
            if (manipMix > 1.0e-4f)
            {
                const float* src = manip ? m : lastManip;
                for (int c = 0; c < 2; ++c) x[c] = x[c] * (1.0f - manipMix) + src[c] * manipMix;
                if (! manip) { lastManip[0] *= 0.995f; lastManip[1] *= 0.995f; }
            }

            // ---- pitch shift (two crossfaded taps) -----------------------------------
            for (int c = 0; c < 2; ++c) pbuf[c][(size_t) pPos] = x[c];
            pitchMix += ((a[pitch] ? 1.0f : 0.0f) - pitchMix) * smooth;
            if (pitchMix > 1.0e-4f)
            {
                const double ratio = std::pow (2.0, pitchSemis[juce::jlimit (0, 5, s.pitchSemi)] / 12.0);
                const double W = 0.045 * sr;
                pPhase += (1.0 - ratio) / W;
                pPhase -= std::floor (pPhase);
                const double ph2 = std::fmod (pPhase + 0.5, 1.0);
                const float g1 = (float) std::pow (std::sin (pi * pPhase), 2.0), g2 = (float) std::pow (std::sin (pi * ph2), 2.0);
                for (int c = 0; c < 2; ++c)
                {
                    auto tap = [&] (double d)
                    {
                        double rp = (double) pPos - d;
                        while (rp < 0.0) rp += pLen;
                        const int i0 = (int) rp;
                        const float fr = (float) (rp - i0);
                        const float v0 = pbuf[c][(size_t) i0], v1 = pbuf[c][(size_t) ((i0 + 1) % pLen)];
                        return v0 + fr * (v1 - v0);
                    };
                    const float y = tap (2.0 + pPhase * W) * g1 + tap (2.0 + ph2 * W) * g2;
                    x[c] = x[c] * (1.0f - pitchMix) + y * pitchMix;
                }
            }
            pPos = (pPos + 1) % pLen;

            // ---- filter -----------------------------------------------------------
            filterMix += ((a[filter] ? 1.0f : 0.0f) - filterMix) * smooth;
            if (filterMix > 1.0e-4f)
            {
                const double el = juce::jmax (0.0, p - engagePpq[filter]);
                double fc = 1000.0, k = 2.0 - 1.85 * s.filtRes;
                float drive = 1.0f;
                switch (s.filtMode)
                {
                    case 0: fc = 30.0 * std::pow (3500.0 / 30.0, juce::jmin (1.0, el / 4.0)); break;          // HP build
                    case 1: fc = 18000.0 * std::pow (220.0 / 18000.0, juce::jmin (1.0, el / 4.0)); break;     // LP drop
                    case 2:                                                                                   // acid
                    {
                        lfoPhase = std::fmod (p, 0.5) / 0.5;
                        const double lfo = 0.5 - 0.5 * std::cos (2.0 * pi * lfoPhase);
                        fc = 220.0 * std::pow (2.0, lfo * 4.6);
                        k = 2.0 - 1.95 * juce::jmax (0.6, (double) s.filtRes);
                        drive = 2.5f;
                        break;
                    }
                    default: fc = 1600.0; k = 1.2 - 0.6 * s.filtRes; drive = 2.0f; break;                     // phone
                }
                fc = juce::jlimit (20.0, sr * 0.45, fc);
                const double g = std::tan (pi * fc / sr);
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                for (int c = 0; c < 2; ++c)
                {
                    const double v0 = x[c];
                    const double v3 = v0 - ic2[c];
                    const double v1 = a1 * ic1[c] + a2 * v3;
                    const double v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                    ic1[c] = (float) (2.0 * v1 - ic1[c]);
                    ic2[c] = (float) (2.0 * v2 - ic2[c]);
                    double y;
                    if (s.filtMode == 0)      y = v0 - k * v1 - v2;
                    else if (s.filtMode == 3) y = v1 * k;
                    else                      y = v2;
                    float yf = (float) y;
                    if (drive > 1.0f) yf = std::tanh (yf * drive) / std::tanh (drive);
                    x[c] = x[c] * (1.0f - filterMix) + yf * filterMix;
                }
            }

            // ---- crush ------------------------------------------------------------
            crushMix += ((a[crush] ? 1.0f : 0.0f) - crushMix) * smooth;
            if (crushMix > 1.0e-4f)
            {
                const int factor = 1 + (int) (s.crushAmt * 18.0f);
                const float levelsQ = std::pow (2.0f, 13.0f - 10.0f * s.crushAmt);
                if (++crushCount >= factor)
                {
                    crushCount = 0;
                    for (int c = 0; c < 2; ++c) crushHold[c] = std::round (x[c] * levelsQ) / levelsQ;
                }
                const float dr = 1.0f + 3.0f * s.crushAmt;
                for (int c = 0; c < 2; ++c)
                {
                    const float y = std::tanh (crushHold[c] * dr) / std::tanh (dr) * 0.9f;
                    x[c] = x[c] * (1.0f - crushMix) + y * crushMix;
                }
            }

            // ---- gate -------------------------------------------------------------
            {
                const double rate = gateBeats[juce::jlimit (0, 4, s.gateRate)];
                const double ph = std::fmod (p, rate) / rate;
                const float target = a[gate] ? (ph < 0.5 ? 1.0f : 1.0f - s.gateDepth) : 1.0f;
                gateGain += (target - gateGain) * gateSmooth;
                x[0] *= gateGain;
                x[1] *= gateGain;
            }

            // ---- delay throw (ping-pong, tails keep ringing) ----------------------
            dSend += ((a[delay] ? 1.0f : 0.0f) - dSend) * smooth;
            {
                const double dt = juce::jlimit (1.0, (double) dLen - 2.0, dlyBeats[juce::jlimit (0, 3, s.dlyTime)] * spb);
                int rp = dPos - (int) dt;
                if (rp < 0) rp += dLen;
                const float dl = dbuf[0][(size_t) rp], dr = dbuf[1][(size_t) rp];
                const float fb = juce::jlimit (0.0f, 0.9f, s.dlyFb);
                // highpass the feedback so repeats stay clean
                dlyHp[0] += 0.02f * (dr - dlyHp[0]);
                dlyHp[1] += 0.02f * (dl - dlyHp[1]);
                const float in = 0.5f * (x[0] + x[1]) * dSend;
                dbuf[0][(size_t) dPos] = in + (dr - dlyHp[0]) * fb;
                dbuf[1][(size_t) dPos] = (dl - dlyHp[1]) * fb;
                dPos = (dPos + 1) % dLen;
                x[0] += dl * 0.75f;
                x[1] += dr * 0.75f;
            }

            // ---- reverb send --------------------------------------------------------
            vSend += ((a[reverb] ? 1.0f : 0.0f) - vSend) * smooth;
            sendL[(size_t) k] = x[0] * vSend;
            sendR[(size_t) k] = x[1] * vSend;
            if (a[reverb] && s.verbFreeze && p - engagePpq[reverb] >= 0.5) freezeNow = true;

            for (int c = 0; c < chans; ++c) buffer.setSample (c, i, x[c]);
            ++now;
        }

        // reverb (block)
        const float room = juce::jlimit (0.3f, 0.98f, s.verbSize);
        const float frz = freezeNow ? 1.0f : 0.0f;
        if (std::abs (verbParams.roomSize - room) > 1.0e-4f || verbParams.freezeMode != frz)
        {
            verbParams.roomSize = room;
            verbParams.freezeMode = frz;
            verb.setParameters (verbParams);
        }
        verb.processStereo (sendL.data(), sendR.data(), n);
        for (int k = 0; k < n; ++k)
        {
            const int i = base + k;
            if (chans == 1)
                buffer.setSample (0, i, (buffer.getSample (0, i) + 0.5f * (sendL[(size_t) k] + sendR[(size_t) k]) * 0.6f) * s.outGain);
            else
            {
                buffer.setSample (0, i, (buffer.getSample (0, i) + sendL[(size_t) k] * 0.6f) * s.outGain);
                buffer.setSample (1, i, (buffer.getSample (1, i) + sendR[(size_t) k] * 0.6f) * s.outGain);
            }
        }
    }
}

//==============================================================================
juce::AudioBuffer<float> makeReverseSwell (const juce::AudioBuffer<float>& clip, double sampleRate, int swellSamples)
{
    juce::AudioBuffer<float> out (2, juce::jmax (1, swellSamples));
    out.clear();
    if (swellSamples <= 0 || clip.getNumSamples() == 0) return out;

    // reverse the opening of the clip, reverb it, keep the tail, reverse the tail
    const int seg = juce::jmin (clip.getNumSamples(), (int) (sampleRate * 1.5));
    const int total = seg + swellSamples;
    juce::AudioBuffer<float> work (2, total);
    work.clear();
    for (int c = 0; c < 2; ++c)
    {
        const float* src = clip.getReadPointer (juce::jmin (c, clip.getNumChannels() - 1));
        for (int i = 0; i < seg; ++i) work.setSample (c, i, src[seg - 1 - i]);
    }
    juce::Reverb rv;
    rv.setSampleRate (sampleRate);
    juce::Reverb::Parameters pr;
    pr.roomSize = 0.93f; pr.damping = 0.3f; pr.wetLevel = 0.3f; pr.dryLevel = 0.0f; pr.width = 1.0f;
    rv.setParameters (pr);
    rv.processStereo (work.getWritePointer (0), work.getWritePointer (1), total);

    float peak = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < swellSamples; ++i)
        {
            const float v = work.getSample (c, total - 1 - i);   // reversed tail
            const float t = (float) i / (float) swellSamples;
            const float shape = std::pow (t, 1.6f);              // grows into the clip
            out.setSample (c, i, v * shape);
            peak = juce::jmax (peak, std::abs (v * shape));
        }
    const float clipPeak = clip.getMagnitude (0, clip.getNumSamples());
    if (peak > 1.0e-6f) out.applyGain (0.7f * clipPeak / peak);
    return out;
}
}
