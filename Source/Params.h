#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP.h"

namespace ts
{
// slot "type" parameter: 0 = empty, 1..3 = ts::Type + 1
enum SlotType { kEmpty = 0 };
inline constexpr int numSlots() { return 8; }
inline int slotTypeOf (int t) { return t + 1; }
inline int tsTypeOf (int slotType) { return slotType - 1; }
inline const char* typeTitle (int t) { static const char* n[] = { "TAPE MACHINE", "ECHO / DELAY", "SPACE REVERB" }; return n[clampv (t, 0, 2)]; }

// ----------------------------------------------------------------- per-slot parameters (IDs = "s<slot>_<key>", keys = web keys)
struct PSpec
{
    const char* key; const char* name; int kind;    // 0 float, 1 bool, 2 int
    double lo, hi, step, def; double skewCentre; const char* unit;
    double Params::* field;
};
enum { kF = 0, kB = 1, kI = 2 };
inline const std::vector<PSpec>& paramSpecs()
{
    static const std::vector<PSpec> v {
        { "variant", "Model", kI, 0, 4, 1, 0, 0, "", &Params::variant },
        { "mix", "Mix", kF, 0, 100, 1, 100, 0, "%", &Params::mix },
        { "output", "Output", kF, -24, 24, 0.1, 0, 0, "dB", &Params::output },
        { "analog", "Analog", kB, 0, 1, 1, 1, 0, "", &Params::analog },
        { "c1", "Electronics", kI, 0, 2, 1, 0, 0, "", &Params::c1 },
        { "c2", "Heads", kI, 0, 2, 1, 0, 0, "", &Params::c2 },
        { "c3", "Alignment", kI, 0, 2, 1, 0, 0, "", &Params::c3 },
        { "age", "Age", kF, 0, 100, 1, 0, 0, "%", &Params::age },
        { "headroom", "Headroom", kF, 12, 36, 0.5, 24, 0, "V", &Params::headroom },
        { "drive", "Input / Drive", kF, -12, 18, 0.1, 0, 0, "dB", &Params::drive },
        { "speed", "Tape Speed", kF, 3.75, 30, 0, 15, 10, "ips", &Params::speed },
        { "eqStd", "EQ CCIR", kI, 0, 1, 1, 0, 0, "", &Params::eqStd },
        { "formula", "Tape Formula", kI, 0, 2, 1, 0, 0, "", &Params::formula },
        { "bias", "Bias", kF, -100, 100, 1, 0, 0, "", &Params::bias },
        { "wow", "Wow", kF, 0, 100, 1, 0, 0, "", &Params::wow },
        { "flutter", "Flutter", kF, 0, 100, 1, 0, 0, "", &Params::flutter },
        { "hiss", "Hiss / Noise", kF, 0, 100, 1, 0, 0, "", &Params::hiss },
        { "time", "Delay Time", kF, 1, 4000, 0, 375, 300, "ms", &Params::time },
        { "sync", "Sync", kI, 0, 14, 1, 0, 0, "", &Params::sync },
        { "feedback", "Feedback", kF, 0, 110, 1, 35, 0, "%", &Params::feedback },
        { "stereo", "Stereo Mode", kI, 0, 3, 1, 1, 0, "", &Params::stereo },
        { "ratioLR", "R/L Ratio", kF, 10, 200, 1, 75, 0, "%", &Params::ratioLR },
        { "lowcut", "Low Cut", kF, 10, 1000, 0, 80, 100, "Hz", &Params::lowcut },
        { "highcut", "High Cut", kF, 1000, 20000, 0, 12000, 4500, "Hz", &Params::highcut },
        { "modRate", "Mod Rate", kF, 0.05, 10, 0, 0.5, 0.7, "Hz", &Params::modRate },
        { "modDepth", "Mod Depth", kF, 0, 100, 1, 0, 0, "", &Params::modDepth },
        { "duck", "Duck", kF, 0, 100, 1, 0, 0, "%", &Params::duck },
        { "heads", "Playback Heads", kI, 0, 6, 1, 0, 0, "", &Params::heads },
        { "source", "Reverb Source", kI, 0, 2, 1, 0, 0, "", &Params::source },
        { "irIndex", "Impulse", kI, 0, 6, 1, 0, 0, "", &Params::irIndex },
        { "predelay", "Pre-delay", kF, 0, 250, 1, 20, 0, "ms", &Params::predelay },
        { "size", "Size", kF, 20, 200, 1, 100, 0, "%", &Params::size },
        { "decay", "Decay", kF, 0.1, 30, 0, 2.2, 2, "s", &Params::decay },
        { "hiMult", "High Decay x", kF, 0.05, 2, 0, 0.55, 0.4, "x", &Params::hiMult },
        { "loMult", "Low Decay x", kF, 0.25, 4, 0, 1.2, 1, "x", &Params::loMult },
        { "diffusion", "Diffusion", kF, 0, 100, 1, 80, 0, "%", &Params::diffusion },
        { "er", "Early Reflections", kF, 0, 100, 1, 50, 0, "%", &Params::er },
        { "modulation", "Modulation", kF, 0, 100, 1, 30, 0, "%", &Params::modulation },
        { "width", "Width", kF, 0, 200, 1, 100, 0, "%", &Params::width },
        { "reverse", "Reverse", kB, 0, 1, 1, 0, 0, "", &Params::reverse },
        { "revLen", "Reverse Length", kF, 100, 6000, 10, 1500, 0, "ms", &Params::revLen },
        { "irLen", "IR Length", kF, 5, 100, 1, 100, 0, "%", &Params::irLen },
        { "eqLo", "Wet EQ Low", kF, -12, 12, 0.1, 0, 0, "dB", &Params::eqLo },
        { "eqMid", "Wet EQ Mid", kF, -12, 12, 0.1, 0, 0, "dB", &Params::eqMid },
        { "eqMidF", "Wet EQ Mid Freq", kF, 200, 8000, 0, 1500, 1265, "Hz", &Params::eqMidF },
        { "eqHi", "Wet EQ High", kF, -12, 12, 0.1, 0, 0, "dB", &Params::eqHi },
    };
    return v;
}
inline const PSpec* specOf (const juce::String& key)
{
    for (auto& s : paramSpecs()) if (key == s.key) return &s;
    return nullptr;
}
namespace id
{
    inline juce::String of (int slot, const char* key) { return "s" + juce::String (slot + 1) + "_" + key; }
    inline const char* type = "type"; inline const char* on = "on";
}
inline const std::vector<const char*>& slotKeys()
{
    static const std::vector<const char*> v = [] { std::vector<const char*> k { id::type, id::on }; for (auto& s : paramSpecs()) k.push_back (s.key); return k; }();
    return v;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    for (int s = 0; s < numSlots(); ++s)
    {
        auto P = [s] (const char* key) { return ParameterID { id::of (s, key), 1 }; };
        const String pre = "S" + String (s + 1) + " ";
        L.add (std::make_unique<AudioParameterChoice> (P (id::type), pre + "Module", StringArray { "Empty", "Tape Machine", "Echo / Delay", "Space Reverb" },
                                                       s == 0 ? slotTypeOf (kTape) : s == 1 ? slotTypeOf (kDelay) : s == 2 ? slotTypeOf (kReverb) : 0));
        L.add (std::make_unique<AudioParameterBool> (P (id::on), pre + "On", true));
        for (auto& sp : paramSpecs())
        {
            const String name = pre + sp.name;
            if (sp.kind == kB) { L.add (std::make_unique<AudioParameterBool> (P (sp.key), name, sp.def > 0.5)); continue; }
            if (sp.kind == kI) { L.add (std::make_unique<AudioParameterInt> (P (sp.key), name, (int) sp.lo, (int) sp.hi, (int) sp.def)); continue; }
            NormalisableRange<float> r ((float) sp.lo, (float) sp.hi, (float) sp.step);
            if (sp.skewCentre > 0) r.setSkewForCentre ((float) sp.skewCentre);
            L.add (std::make_unique<AudioParameterFloat> (P (sp.key), name, r, (float) sp.def, AudioParameterFloatAttributes().withLabel (sp.unit)));
        }
    }
    return L;
}

struct ParamReader
{
    explicit ParamReader (juce::AudioProcessorValueTreeState& s) : st (s)
    {
        for (int sl = 0; sl < numSlots(); ++sl)
        {
            typeP[(size_t) sl] = st.getRawParameterValue (id::of (sl, id::type));
            onP[(size_t) sl] = st.getRawParameterValue (id::of (sl, id::on));
            for (size_t k = 0; k < paramSpecs().size(); ++k) raw[(size_t) sl].push_back (st.getRawParameterValue (id::of (sl, paramSpecs()[k].key)));
        }
    }
    int slotType (int s) const { return (int) std::lround (typeP[(size_t) s]->load()); }
    int type (int s) const { return tsTypeOf (slotType (s)); }          // −1 = empty
    bool on (int s) const { return slotType (s) != kEmpty && onP[(size_t) s]->load() > 0.5f; }
    Params params (int s) const
    {
        Params p;
        const auto& sp = paramSpecs();
        for (size_t k = 0; k < sp.size(); ++k)
        {
            double v = raw[(size_t) s][k]->load();
            if (sp[k].kind != kF) v = (double) std::lround (v);
            p.*(sp[k].field) = v;
        }
        return p;
    }
    juce::AudioProcessorValueTreeState& st;
    std::array<std::atomic<float>*, (size_t) numSlots()> typeP {}, onP {};
    std::array<std::vector<std::atomic<float>*>, (size_t) numSlots()> raw;
};
} // namespace ts
