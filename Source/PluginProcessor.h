#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Presets.h"

// A user impulse response loaded from a file (kept at its own sample rate; resampled when the engine needs it)
struct UserIR
{
    juce::String name, path;
    double fs = 48000;
    ts::IR data;
    int version = 0;
    bool empty() const { return data[0].empty(); }
};

class TapeSpaceRackProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    TapeSpaceRackProcessor();
    ~TapeSpaceRackProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    // one program only: a VST3 "Program" parameter would push Ableton Live over its 64-parameter auto-list limit.
    // Rack presets are in the plug-in's own menu.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassPrm; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState state;

    // ---- metering, read by the editor (lock-free, max-hold until read)
    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    std::array<std::atomic<float>, (size_t) ts::numSlots()> lvIn {}, lvOut {}, lvWet {};
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> bpmFromHost { false };

    // ---- output spectrum FIFO (mono sum, host rate)
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::AbstractFifo specFifo { fftSize * 4 };
    std::vector<float> specBuffer = std::vector<float> ((size_t) fftSize * 4);
    double getHostRate() const { return hostRate; }

    // ---- rack editing (message thread). Slots are packed: used slots first, in signal order.
    int usedSlots() const;
    bool addModule (int tsType);
    void removeSlot (int slot);
    void duplicateSlot (int slot);
    void moveSlot (int from, int to);
    void applyModulePreset (int slot, int index);
    void applyRackPreset (int index);
    void setPlain (int slot, const char* key, double value);
    double getPlain (int slot, const char* key) const;
    const ts::PresetBank presets;
    int currentProgram = 0;
    ts::SlotStore store;                                     // the rack (all modules, plain values)
    ts::ParamReader reader { state, store };

    // ---- user impulse responses (message thread; the worker reads them under the lock)
    juce::String loadUserIR (int slot, const juce::File& file);         // returns an error text, empty on success
    UserIR userIRCopy (int slot) const { const juce::ScopedLock sl (irLock); return userIR[(size_t) slot]; }
    juce::String userIRName (int slot) const { const juce::ScopedLock sl (irLock); return userIR[(size_t) slot].name; }
    // the IR currently used by a reverb slot (for the editor's views), at the host rate
    ts::IR currentIR (int slot) const { const juce::ScopedLock sl (irLock); return builtIR[(size_t) slot]; }
    int irSerial (int slot) const { return irSerials[(size_t) slot].load(); }

private:
    void run() override;                                  // worker: builds modules and impulse responses off the audio thread
    void buildSlot (int s, bool publish);
    std::string irKeyOf (int s, const ts::Params& p) const;
    ts::IR resampledUserIR (int s, double fs);
    std::vector<double> readSlot (int slot) const;
    void writeSlot (int slot, const std::vector<double>& values);
    std::vector<double> slotValues (int tsType, const ts::Params& p, bool on = true) const;
    void setParam (juce::RangedAudioParameter* p, float normalised);
    void flushHost();                                      // host parameters → store (first module of each type)
    void pushHost();                                       // store → host parameters
    void editRack (const std::function<void()>& fn) { flushHost(); fn(); pushHost(); markDirty(); }
    void markDirty();
    std::vector<juce::RangedAudioParameter*> hostPrm;
    juce::AudioProcessorParameter* bypassPrm = nullptr;
    void permuteUserIR (const std::function<void (std::array<UserIR, (size_t) ts::numSlots()>&)>& fn);

    // deferred deletion: the audio thread hands objects to the worker through a lock-free FIFO
    struct Trash { ts::Module* m = nullptr; ts::ConvPair* c = nullptr; };
    static constexpr int kTrash = 256;
    juce::AbstractFifo trashFifo { kTrash };
    std::array<Trash, kTrash> trash {};
    void toTrash (ts::Module* m, ts::ConvPair* c);
    void trashModule (ts::Module* m);
    void emptyTrash();

    std::array<ts::Module*, (size_t) ts::numSlots()> live {};               // audio thread owns these
    std::array<std::atomic<ts::Module*>, (size_t) ts::numSlots()> pendingMod {};
    std::array<std::atomic<ts::ConvPair*>, (size_t) ts::numSlots()> pendingIR {};
    std::array<std::atomic<int>, (size_t) ts::numSlots()> liveGen {};
    std::array<ts::Params, (size_t) ts::numSlots()> lastP {};
    std::array<bool, (size_t) ts::numSlots()> lastValid {};
    struct Alloc { int type = -2; std::string irKey; double fs = 0; };
    std::array<Alloc, (size_t) ts::numSlots()> alloc {};                     // worker's view of what it built
    std::array<UserIR, (size_t) ts::numSlots()> userIR;
    std::array<ts::IR, (size_t) ts::numSlots()> builtIR;
    std::array<std::atomic<int>, (size_t) ts::numSlots()> irSerials {};
    juce::CriticalSection irLock, buildLock;
    int genSeq = 1;
    juce::AudioBuffer<float> monoScratch;
    double hostRate = 48000;
    std::atomic<bool> prepared { false };
    double lastBpm = 120;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeSpaceRackProcessor)
};
