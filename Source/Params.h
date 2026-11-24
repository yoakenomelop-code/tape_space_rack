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
        { "time", "Delay Time", kF, 1, 8000, 0, 375, 300, "ms", &Params::time },
        { "sync", "Delay Sync", kI, 0, 78, 1, 0, 0, "", &Params::sync },
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
        { "predelay", "Pre-delay", kF, 0, 1000, 0, 20, 60, "ms", &Params::predelay },
        { "preSync", "Pre-delay Sync", kI, 0, 78, 1, 0, 0, "", &Params::preSync },
        { "decaySync", "Decay Sync", kI, 0, 78, 1, 0, 0, "", &Params::decaySync },
        { "revSync", "Reverse Length Sync", kI, 0, 78, 1, 0, 0, "", &Params::revSync },
        { "size", "Size", kF, 20, 200, 1, 100, 0, "%", &Params::size },
        { "decay", "Decay", kF, 0.1, 30, 0, 2.2, 2, "s", &Params::decay },
        { "hiMult", "High Decay x", kF, 0.05, 2, 0, 0.55, 0.4, "x", &Params::hiMult },
        { "loMult", "Low Decay x", kF, 0.25, 4, 0, 1.2, 1, "x", &Params::loMult },
        { "diffusion", "Diffusion", kF, 0, 100, 1, 80, 0, "%", &Params::diffusion },
        { "er", "Early Reflections", kF, 0, 100, 1, 50, 0, "%", &Params::er },
        { "modulation", "Modulation", kF, 0, 100, 1, 30, 0, "%", &Params::modulation },
        { "width", "Width", kF, 0, 200, 1, 100, 0, "%", &Params::width },
        { "reverse", "Reverse", kB, 0, 1, 1, 0, 0, "", &Params::reverse },
        { "revLen", "Reverse Length", kF, 100, 6000, 0, 1500, 1000, "ms", &Params::revLen },
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
    inline const char* type = "type"; inline const char* on = "on";
}
// every per-slot key, in a fixed order (type, on, then the parameter keys)
inline const std::vector<const char*>& slotKeys()
{
    static const std::vector<const char*> v = [] { std::vector<const char*> k { id::type, id::on }; for (auto& s : paramSpecs()) k.push_back (s.key); return k; }();
    return v;
}
inline int keyIndex (const juce::String& key) { const auto& k = slotKeys(); for (size_t i = 0; i < k.size(); ++i) if (key == k[i]) return (int) i; return -1; }

// ----------------------------------------------------------------- rack storage (not host parameters)
// The rack (up to 8 modules in any order) lives here. Values are plain (real units), atomics so the audio thread can read them.
struct SlotStore
{
    static constexpr int NK = 2 + 49;   // type, on, parameter keys (checked below)
    std::array<std::array<std::atomic<double>, (size_t) NK>, (size_t) numSlots()> v;
    SlotStore()
    {
        jassert ((int) slotKeys().size() == NK);
        const Params d;
        for (auto& sl : v)
        {
            sl[0] = 0; sl[1] = 1;
            for (size_t k = 0; k < paramSpecs().size(); ++k) sl[k + 2] = d.*(paramSpecs()[k].field);
        }
    }
};

// ----------------------------------------------------------------- host (automation) parameters: exactly 64
// Ableton Live lists every parameter of a plug-in automatically only when it has 64 or fewer, so the host sees one fixed,
// clearly named set per module type. Each set drives the FIRST module of that type in the rack.
struct HostParam { int type; const char* key; const char* name; };
inline const std::vector<HostParam>& hostParams()
{
    static const std::vector<HostParam> v {
        { kTape, "on", "Tape On" }, { kTape, "variant", "Tape Machine" }, { kTape, "drive", "Tape Input" }, { kTape, "formula", "Tape Formula" },
        { kTape, "bias", "Tape Bias" }, { kTape, "speed", "Tape Speed" }, { kTape, "eqStd", "Tape EQ CCIR" }, { kTape, "wow", "Tape Wow" },
        { kTape, "flutter", "Tape Flutter" }, { kTape, "hiss", "Tape Hiss" }, { kTape, "analog", "Tape Analog" }, { kTape, "mix", "Tape Mix" },
        { kTape, "output", "Tape Output" },
        { kDelay, "on", "Delay On" }, { kDelay, "variant", "Delay Model" }, { kDelay, "time", "Delay Time" }, { kDelay, "sync", "Delay Sync" },
        { kDelay, "feedback", "Delay Feedback" }, { kDelay, "lowcut", "Delay Low Cut" }, { kDelay, "highcut", "Delay High Cut" }, { kDelay, "drive", "Delay Drive" },
        { kDelay, "stereo", "Delay Stereo Mode" }, { kDelay, "ratioLR", "Delay R/L Ratio" }, { kDelay, "width", "Delay Width" }, { kDelay, "reverse", "Delay Reverse" },
        { kDelay, "modRate", "Delay Mod Rate" }, { kDelay, "modDepth", "Delay Mod Depth" }, { kDelay, "duck", "Delay Duck" }, { kDelay, "hiss", "Delay Noise" },
        { kDelay, "heads", "Delay Heads" }, { kDelay, "analog", "Delay Analog" }, { kDelay, "eqLo", "Delay EQ Low" }, { kDelay, "eqMid", "Delay EQ Mid" },
        { kDelay, "eqMidF", "Delay EQ Mid Freq" }, { kDelay, "eqHi", "Delay EQ High" }, { kDelay, "mix", "Delay Mix" }, { kDelay, "output", "Delay Output" },
        { kReverb, "on", "Reverb On" }, { kReverb, "variant", "Reverb Algorithm" }, { kReverb, "source", "Reverb Source" }, { kReverb, "irIndex", "Reverb Impulse" },
        { kReverb, "predelay", "Reverb Pre-delay" }, { kReverb, "preSync", "Reverb Pre-delay Sync" }, { kReverb, "size", "Reverb Size" }, { kReverb, "decay", "Reverb Decay" },
        { kReverb, "decaySync", "Reverb Decay Sync" }, { kReverb, "loMult", "Reverb Low Decay x" }, { kReverb, "hiMult", "Reverb High Decay x" }, { kReverb, "diffusion", "Reverb Diffusion" },
        { kReverb, "er", "Reverb Early Ref" }, { kReverb, "modulation", "Reverb Modulation" }, { kReverb, "reverse", "Reverb Reverse" }, { kReverb, "revLen", "Reverb Reverse Length" },
        { kReverb, "revSync", "Reverb Reverse Sync" }, { kReverb, "irLen", "Reverb IR Length" }, { kReverb, "lowcut", "Reverb Low Cut" }, { kReverb, "highcut", "Reverb High Cut" },
        { kReverb, "eqLo", "Reverb EQ Low" }, { kReverb, "eqMid", "Reverb EQ Mid" }, { kReverb, "eqMidF", "Reverb EQ Mid Freq" }, { kReverb, "eqHi", "Reverb EQ High" },
        { kReverb, "width", "Reverb Width" }, { kReverb, "mix", "Reverb Mix" }, { kReverb, "output", "Reverb Output" },
    };
    return v;
}
inline juce::String hostId (int type, const char* key) { return juce::String (typeKey (type)) + "_" + key; }
// host parameter index for (type, slot key index), −1 when that key is not a host parameter
inline int hostIndex (int type, int keyIdx)
{
    static const std::array<std::vector<int>, 3> m = []
    {
        std::array<std::vector<int>, 3> a; for (auto& x : a) x.assign (slotKeys().size(), -1);
        const auto& hp = hostParams();
        for (size_t i = 0; i < hp.size(); ++i) a[(size_t) hp[i].type][(size_t) keyIndex (hp[i].key)] = (int) i;
        return a;
    }();
    if (type < 0 || type > 2 || keyIdx < 0 || keyIdx >= (int) slotKeys().size()) return -1;
    return m[(size_t) type][(size_t) keyIdx];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    const char* groupNames[3] = { "Tape", "Delay", "Reverb" };
    for (int t = 0; t < 3; ++t)
    {
        auto g = std::make_unique<AudioProcessorParameterGroup> (typeKey (t), groupNames[t], " | ");
        const Params d = defaultsFor (t);
        for (auto& hp : hostParams())
        {
            if (hp.type != t) continue;
            const ParameterID pid { hostId (t, hp.key), 1 };
            if (String (hp.key) == "on") { g->addChild (std::make_unique<AudioParameterBool> (pid, hp.name, true)); continue; }
            const auto* sp = specOf (hp.key);
            const double def = d.*(sp->field);
            if (sp->kind == kB) { g->addChild (std::make_unique<AudioParameterBool> (pid, hp.name, def > 0.5)); continue; }
            if (sp->kind == kI)
            {
                int hi = (int) sp->hi;
                if (String (hp.key) == "variant") hi = t == kTape ? 2 : t == kDelay ? 3 : 4;
                g->addChild (std::make_unique<AudioParameterInt> (pid, hp.name, (int) sp->lo, hi, (int) def)); continue;
            }
            NormalisableRange<float> r ((float) sp->lo, (float) sp->hi, (float) sp->step);
            if (sp->skewCentre > 0) r.setSkewForCentre ((float) sp->skewCentre);
            g->addChild (std::make_unique<AudioParameterFloat> (pid, hp.name, r, (float) def, AudioParameterFloatAttributes().withLabel (sp->unit)));
        }
        L.add (std::move (g));
    }
    return L;
}

// read the rack: the first module of each type reads its host parameters, everything else reads the store
struct ParamReader
{
    ParamReader (juce::AudioProcessorValueTreeState& s, SlotStore& st) : store (st)
    {
        const auto& hp = hostParams();
        for (size_t i = 0; i < hp.size(); ++i) host.push_back (s.getRawParameterValue (hostId (hp[(size_t) i].type, hp[i].key)));
    }
    int slotType (int s) const { return (int) std::lround (store.v[(size_t) s][0].load()); }
    int type (int s) const { return tsTypeOf (slotType (s)); }          // −1 = empty
    int mappedSlot (int t) const { for (int s = 0; s < numSlots(); ++s) if (type (s) == t) return s; return -1; }
    // the host parameter that currently drives (slot, key index), or nullptr
    std::atomic<float>* hostFor (int s, int k) const
    {
        const int t = type (s); if (t < 0 || mappedSlot (t) != s) return nullptr;
        const int h = hostIndex (t, k); return h >= 0 ? host[(size_t) h] : nullptr;
    }
    double value (int s, int k) const
    {
        if (auto* h = hostFor (s, k))
        {
            double v = h->load();
            if (k < 2 || paramSpecs()[(size_t) k - 2].kind != kF) v = (double) std::lround (v);
            return v;
        }
        return store.v[(size_t) s][(size_t) k].load();
    }
    bool on (int s) const { return slotType (s) != kEmpty && value (s, 1) > 0.5; }
    Params params (int s) const
    {
        Params p;
        const auto& sp = paramSpecs();
        for (size_t k = 0; k < sp.size(); ++k)
        {
            double v = value (s, (int) k + 2);
            if (sp[k].kind != kF) v = (double) std::lround (v);
            p.*(sp[k].field) = v;
        }
        return p;
    }
    SlotStore& store;
    std::vector<std::atomic<float>*> host;
};
} // namespace ts
