#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

// Tempo-synced vocal FX engine. Used live (processBlock) and offline (print/render).
namespace vfx
{
    enum Fx { stutter = 0, reverse, tapeStop, glitch, pitch, filter, crush, gate, delay, reverb, numFx };

    const char* fxName (int fx);

    // user settings (index values match the UI choice lists)
    struct Settings
    {
        int stutDiv = 3;       // 1/2, 1/4, 1/8, 1/16, 1/32, RAMP
        int revDiv = 1;        // 1/2, 1/4, 1/8, 1 BAR
        int tapeLen = 1;       // 1/4, 1/2, 1 BAR, 2 BARS
        float glitchAmt = 0.6f;
        int glitchSeed = 7;
        int pitchSemi = 0;     // -12, -7, -5, +5, +7, +12
        int filtMode = 0;      // HP BUILD, LP DROP, ACID, PHONE
        float filtRes = 0.5f;
        float crushAmt = 0.6f;
        int gateRate = 1;      // 1/8, 1/16, 1/32, 1/8T, 1/16T
        float gateDepth = 1.0f;
        int dlyTime = 2;       // 1/4, 1/8D, 1/8, 1/16
        float dlyFb = 0.55f;
        float verbSize = 0.85f;
        bool verbFreeze = false;
        float outGain = 1.0f;
    };

    // which FX are on: latched/pad state + step sequencer
    struct Activity
    {
        std::array<bool, numFx> on {};
        std::array<juce::uint16, numFx> seq {};   // 16 steps per row, bit i = step i
        bool seqEnabled = false;
        double stepBeats = 0.25;                  // 1/16 = 0.25 beats per step

        bool isActive (int fx, double ppq) const
        {
            if (on[(size_t) fx]) return true;
            if (! seqEnabled) return false;
            const int step = ((int) std::floor (ppq / stepBeats)) & 15;
            return (seq[(size_t) fx] >> step) & 1;
        }
    };

    class Engine
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset();

        // ppq = beat position of the first sample, bpm = tempo
        void process (juce::AudioBuffer<float>& buffer, int numSamples, double ppq, double bpm,
                      const Activity&, const Settings&);

        // how loud each FX currently is (0..1) for the UI
        float getLevel (int fx) const noexcept { return levels[(size_t) fx]; }

    private:
        float readHist (int ch, double pos) const;   // pos = absolute sample index (fractional)
        float stepDiv (int fx, const Settings&, double bpm) const;

        double sr = 44100.0;
        std::vector<float> hist[2];
        juce::int64 histLen = 0, now = 0;            // absolute sample counter

        // per-FX state
        std::array<bool, numFx> prevActive {};
        std::array<double, numFx> engagePpq {};
        std::array<juce::int64, numFx> engageSample {};
        std::array<float, numFx> levels {};

        double tapeRead = 0.0, tapeSpeed = 1.0;
        float manipMix = 0.0f;
        float lastManip[2] {};
        juce::int64 glitchStep = -1;
        int glitchOp = 0;
        juce::int64 glitchStart = 0;

        // pitch shifter
        std::vector<float> pbuf[2];
        int pPos = 0;
        double pPhase = 0.0;
        float pitchMix = 0.0f;

        // filter (TPT state variable)
        float ic1[2] {}, ic2[2] {};
        float filterMix = 0.0f;
        double lfoPhase = 0.0;

        // crush
        float crushHold[2] {};
        int crushCount = 0;
        float crushMix = 0.0f;

        // gate
        float gateGain = 1.0f;

        // delay
        std::vector<float> dbuf[2];
        int dPos = 0;
        float dSend = 0.0f, dlyHp[2] {};

        // reverb (send)
        juce::Reverb verb;
        juce::Reverb::Parameters verbParams;
        std::vector<float> sendL, sendR;
        float vSend = 0.0f;
    };

    // offline helpers
    // reverse-reverb swell that leads into the start of `clip` (length = swellSamples)
    juce::AudioBuffer<float> makeReverseSwell (const juce::AudioBuffer<float>& clip, double sampleRate, int swellSamples);
}
