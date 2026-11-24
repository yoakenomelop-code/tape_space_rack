// Offscreen check: slot editing, presets, user IR, state, audio through the plugin, then render the editor to PNG.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
using namespace ts;
static juce::String types (TapeSpaceRackProcessor& p) { juce::String s; for (int i = 0; i < numSlots(); ++i) s << p.reader.type (i) << " "; return s; }
static void run (TapeSpaceRackProcessor& proc, int blocks, float& maxOut, bool& nan, double& energy, int sleepEvery = 0)
{
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; juce::Random rnd (1);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i) { const double t = (b * 512 + i) / 48000.0; const float s = (float) (0.3 * std::sin (2 * juce::MathConstants<double>::pi * 220 * t) * ((b / 20) % 4 == 0 ? 1.0 : 0.0)); buf.setSample (0, i, s); buf.setSample (1, i, s * 0.8f); }
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); nan |= ! std::isfinite (v); maxOut = std::max (maxOut, std::abs (v)); energy += (double) v * v; }
        if (sleepEvery > 0 && b % sleepEvery == 0) juce::Thread::sleep (10);
    }
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "fuzz")
    {
        // like pluginval's automation test: every parameter random, every 32 samples
        for (double sr : { 44100.0, 96000.0 })
        {
            TapeSpaceRackProcessor proc; proc.setPlayConfigDetails (2, 2, sr, 64); proc.prepareToPlay (sr, 64);
            juce::Random rnd (42); juce::AudioBuffer<float> buf (2, 64); juce::MidiBuffer midi; int nanAt = -1; float pk = 0;
            auto params = proc.getParameters();
            for (int b = 0; b < 6000 && nanAt < 0; ++b)
            {
                for (int k = 0; k < 2; ++k) for (auto* prm : params) if (rnd.nextFloat() < 0.5f) prm->setValue (rnd.nextFloat());
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) buf.setSample (c, i, rnd.nextFloat() * 2 - 1);
                proc.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) nanAt = b; else pk = std::max (pk, std::abs (v)); }
                if (b % 20 == 0) juce::Thread::sleep (1);
            }
            std::printf ("fuzz %.0f: nan at block %d, peak %.1f\n", sr, nanAt, pk);
        }
        return 0;
    }
    const juce::String outPath = argc > 1 ? argv[1] : "snapshot.png";
    const int hlBand = argc > 2 ? juce::String (argv[2]).getIntValue() : ts::ui::hlTime;
    const int hlSlot = argc > 3 ? juce::String (argv[3]).getIntValue() : 1;
    const int rackPreset = argc > 4 ? juce::String (argv[4]).getIntValue() : -1;
    TapeSpaceRackProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000, 512);
    proc.prepareToPlay (48000, 512);
    std::printf ("types initially:  %s  s1 drive %.1f (expect 3), s3 decay %.2f (2.4)\n", types (proc).toRawUTF8(), proc.getPlain (0, "drive"), proc.getPlain (2, "decay"));
    float mx = 0; bool nan = false; double e = 0;
    run (proc, 200, mx, nan, e);
    std::printf ("default rack: max %.3f nan %d energy %.1f latency %d\n", mx, (int) nan, e, proc.getLatencySamples());
    proc.addModule (kReverb);                // tape delay reverb reverb
    proc.setPlain (3, "source", 1); proc.setPlain (3, "irIndex", 5);
    proc.duplicateSlot (1);                  // tape delay delay reverb reverb
    proc.setPlain (2, "reverse", 1); proc.setPlain (2, "stereo", 2);
    proc.moveSlot (4, 0);                    // reverb(IR) tape delay delay reverb
    proc.removeSlot (4);                     // reverb(IR) tape delay delay
    std::printf ("after edits:      %s (expect 2 0 1 1 -1 ...), s1 irIndex %d (5), s4 reverse %d (1)\n", types (proc).toRawUTF8(), (int) proc.getPlain (0, "irIndex"), (int) proc.getPlain (3, "reverse"));
    // user IR file
    {
        juce::File wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tsr_test_ir.wav");
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::FileOutputStream> os (wav.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), 44100, 2, 24, {}, 0));
        if (w) os.release();
        juce::AudioBuffer<float> ir (2, 44100); juce::Random r (5);
        for (int i = 0; i < 44100; ++i) { const float env = std::exp (-i / 8000.f); ir.setSample (0, i, (r.nextFloat() - 0.5f) * env); ir.setSample (1, i, (r.nextFloat() - 0.5f) * env); }
        w->writeFromAudioSampleBuffer (ir, 0, 44100); w.reset();
        proc.addModule (kReverb);
        const auto err = proc.loadUserIR (4, wav);
        std::printf ("user IR load: '%s' name '%s' source %d\n", err.toRawUTF8(), proc.userIRName (4).toRawUTF8(), (int) proc.getPlain (4, "source"));
    }
    mx = 0; e = 0; run (proc, 300, mx, nan, e, 4);
    juce::Thread::sleep (400);
    mx = 0; e = 0; run (proc, 300, mx, nan, e, 4);
    const auto ir4 = proc.currentIR (4), ir0 = proc.currentIR (0);
    std::printf ("rack run: max %.3f nan %d energy %.1f | built IR s1 %d samples (7 s church = 336000), s5 user %d (1 s @48k = 48000)\n", mx, (int) nan, e, (int) ir0[0].size(), (int) ir4[0].size());
    juce::MemoryBlock mb; proc.getStateInformation (mb);
    TapeSpaceRackProcessor proc2; proc2.setPlayConfigDetails (2, 2, 48000, 512);
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    proc2.prepareToPlay (48000, 512);
    std::printf ("state %d bytes, restored types %s user IR '%s' built %d\n", (int) mb.getSize(), types (proc2).toRawUTF8(), proc2.userIRName (4).toRawUTF8(), (int) proc2.currentIR (4)[0].size());
    std::printf ("programs: %d, first '%s', last '%s'\n", proc.getNumPrograms(), proc.getProgramName (0).toRawUTF8(), proc.getProgramName (proc.getNumPrograms() - 1).toRawUTF8());
    for (int pr = 0; pr < proc.getNumPrograms(); ++pr)
    {
        proc.applyRackPreset (pr);
        juce::Thread::sleep (150);
        mx = 0; e = 0; run (proc, 120, mx, nan, e, 3);
        std::printf ("  preset %d %-24s types %s max %.3f nan %d energy %.1f\n", pr, proc.getProgramName (pr).toRawUTF8(), types (proc).toRawUTF8(), mx, (int) nan, e);
    }
    proc.prepareToPlay (96000, 256);
    mx = 0; e = 0; run (proc, 200, mx, nan, e);
    std::printf ("96k re-prepare: max %.3f nan %d\n", mx, (int) nan);
    proc.prepareToPlay (48000, 512);
    proc.applyRackPreset (rackPreset >= 0 ? rackPreset : 0);
    if (rackPreset < 0) { proc.setPlain (1, "variant", 1); proc.setPlain (1, "heads", 6); proc.setPlain (1, "reverse", 0); proc.setPlain (2, "reverse", 1); proc.setPlain (2, "revLen", 1200); proc.setPlain (1, "sync", 33); proc.setPlain (2, "preSync", 6); proc.setPlain (2, "revSync", 15); proc.setPlain (2, "decaySync", 21); }
    juce::Thread::sleep (300);
    run (proc, 100, mx, nan, e, 2);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* rack = dynamic_cast<TapeSpaceRackEditor*> (ed.get());
    rack->setMachineOpen (0, true);
    ed->setSize (1240, argc > 5 ? juce::String (argv[5]).getIntValue() : 1900);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i)
    {
        for (int k = 0; k < 4; ++k) { for (int j = 0; j < 512; ++j) { const double t = ((i * 4 + k) * 512 + j) / 48000.0; const float s = (float) (0.3 * std::sin (2 * juce::MathConstants<double>::pi * 330 * t) * (((i * 4 + k) / 6) % 3 == 0 ? 1.0 : 0.0)); buf.setSample (0, j, s); buf.setSample (1, j, s); } proc.processBlock (buf, midi); }
        rack->timerCallback();
        juce::Thread::sleep (15);
    }
    rack->hover (hlSlot, hlBand, juce::String::fromUTF8 ("（スナップショット用のハイライト）"));
    if (argc > 6 && juce::String (argv[6]) == "picker")
        rack->openPicker ("DELAY TIME SYNC", [&proc] { return (int) proc.getPlain (1, "sync"); }, [&proc] (int i) { proc.setPlain (1, "sync", i); });
    std::printf ("sync check: delay sync %d (%s) -> %.3f ms at %.1f BPM; reverb decaySync %d -> RT %.3f s\n", (int) proc.getPlain (1, "sync"), ts::notes()[(size_t) proc.getPlain (1, "sync")].name.c_str(),
                 ts::delayMs (proc.reader.params (1), proc.hostBpm.load()), proc.hostBpm.load(), (int) proc.getPlain (2, "decaySync"), ts::resolveTimes (proc.reader.params (2), proc.hostBpm.load()).decay);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (outPath)); f.deleteFile();
    juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("editor %dx%d -> %s\n", ed->getWidth(), ed->getHeight(), f.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}
