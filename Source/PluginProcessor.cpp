#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ts;

namespace { std::atomic<int> irVersionSeq { 1 }; }

TapeSpaceRackProcessor::TapeSpaceRackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Tape Space Rack worker"),
      state (*this, nullptr, "TapeSpaceRack", createLayout())
{
    for (auto& hp : hostParams()) hostPrm.push_back (state.getParameter (hostId (hp.type, hp.key)));
    // default rack: tape → delay → reverb, each with a musical start
    const char* starts[3] = { "Warm 15 ips NAB", "HQ Dotted Eighth", "Concert Hall" };
    for (int t = 0; t < 3; ++t)
    {
        Params p = defaultsFor (t);
        PresetBank::apply (p, presets.moduleValuesByName (t, starts[t]));
        writeSlot (t, slotValues (t, p));
    }
    pushHost();
}

TapeSpaceRackProcessor::~TapeSpaceRackProcessor()
{
    stopThread (4000);
    emptyTrash();
    for (int s = 0; s < numSlots(); ++s)
    {
        if (live[(size_t) s]) { trashModule (live[(size_t) s]); live[(size_t) s] = nullptr; emptyTrash(); }
        if (auto* m = pendingMod[(size_t) s].exchange (nullptr)) { trashModule (m); emptyTrash(); }
        delete pendingIR[(size_t) s].exchange (nullptr);
    }
}

bool TapeSpaceRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

// ----------------------------------------------------------------------------- deferred deletion
void TapeSpaceRackProcessor::toTrash (Module* m, ConvPair* c)
{
    const auto scope = trashFifo.write (1);
    if (scope.blockSize1 > 0) trash[(size_t) scope.startIndex1] = { m, c };
    else if (scope.blockSize2 > 0) trash[(size_t) scope.startIndex2] = { m, c };
    // a full FIFO would leak the object rather than free memory on the audio thread
}
void TapeSpaceRackProcessor::trashModule (Module* m)
{
    if (m == nullptr) return;
    if (m->type() == kReverb)
    {
        auto* r = static_cast<Reverb*> (m);
        if (auto* a = r->releaseAll()) toTrash (nullptr, a);
        if (auto* a = r->releaseOld()) toTrash (nullptr, a);
        if (auto* a = r->takeFinished()) toTrash (nullptr, a);
    }
    toTrash (m, nullptr);
}
void TapeSpaceRackProcessor::emptyTrash()
{
    const int n = trashFifo.getNumReady();
    if (n <= 0) return;
    const auto scope = trashFifo.read (n);
    auto take = [this] (int start, int size) { for (int i = 0; i < size; ++i) { auto& t = trash[(size_t) (start + i)]; delete t.m; delete t.c; t = {}; } };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
}

// ----------------------------------------------------------------------------- impulse responses
std::string TapeSpaceRackProcessor::irKeyOf (int s, const Params& p0) const
{
    const Params p = resolveTimes (p0, hostBpm.load());
    if (! (ival (p.source) > 0 || p.reverse != 0)) return {};
    const int src = ival (p.source);
    juce::String k;
    if (src == 0) k << "algo/" << p.variant << "/" << p.size << "/" << p.decay << "/" << p.hiMult << "/" << p.loMult << "/" << p.diffusion << "/" << p.er;
    else if (src == 1) k << "ir/" << p.irIndex;
    else { const juce::ScopedLock sl (irLock); k << "user/" << userIR[(size_t) s].version; }
    k << "/" << p.irLen << "/" << p.reverse << "/" << (p.reverse != 0 ? p.revLen : 0.0);
    return k.toStdString();
}
IR TapeSpaceRackProcessor::resampledUserIR (int s, double fs)
{
    const UserIR u = userIRCopy (s);
    if (u.empty()) return {};
    if (std::abs (u.fs - fs) < 1e-6) return u.data;
    const double ratio = u.fs / fs;
    const int nOut = (int) std::floor ((double) u.data[0].size() / ratio);
    IR out;
    for (int c = 0; c < 2; ++c)
    {
        const auto& src = u.data[(size_t) c].empty() ? u.data[0] : u.data[(size_t) c];
        std::vector<float> padded (src); padded.resize (src.size() + 256, 0.f);       // flush the interpolator's tail
        out[(size_t) c].assign ((size_t) nOut, 0.f);
        juce::WindowedSincInterpolator ip;
        ip.process (ratio, padded.data(), out[(size_t) c].data(), nOut);
    }
    return out;
}
void TapeSpaceRackProcessor::buildSlot (int s, bool publish)
{
    const auto si = (size_t) s;
    const int t = reader.type (s);
    const Params p = reader.params (s);
    const double fs = hostRate;
    auto& a = alloc[si];
    auto makeIR = [&] (int gen) -> ConvPair*
    {
        IR ir;
        if (ival (p.source) == 2) { IR user = resampledUserIR (s, fs); ir = buildIR (p, fs, user[0].empty() ? nullptr : &user, hostBpm.load()); }
        else ir = buildIR (p, fs, nullptr, hostBpm.load());
        { const juce::ScopedLock sl (irLock); builtIR[si] = ir; }
        ++irSerials[si];
        if (ir[0].empty()) return nullptr;
        auto* c = new ConvPair (ir[0], ir[1]); c->gen = gen;
        return c;
    };
    if (a.type != t || a.fs != fs)
    {
        if (t >= 0 && publish && pendingMod[si].load() != nullptr) return;          // the previous one is not installed yet
        std::string key;
        Module* m = nullptr;
        if (t >= 0)
        {
            m = create (t, fs).release();
            m->gen = genSeq++;
            m->setBpm (hostBpm.load());
            m->setParams (p);
            if (t == kDelay) static_cast<Delay*> (m)->snap();
            if (t == kReverb) { key = irKeyOf (s, p); if (auto* c = makeIR (m->gen)) static_cast<Reverb*> (m)->setIR (c); }
        }
        delete pendingIR[si].exchange (nullptr);
        if (m != nullptr)
        {
            if (publish) pendingMod[si].store (m);
            else { if (live[si]) { trashModule (live[si]); emptyTrash(); } live[si] = m; lastValid[si] = false; }
        }
        a.type = t; a.fs = fs; a.irKey = key;
        return;
    }
    if (t == kReverb)
    {
        const auto key = irKeyOf (s, p);
        if (key != a.irKey)
        {
            // the IR is for the newest module of this slot: the one waiting to be installed, else the live one
            Module* owner = pendingMod[si].load();
            const int gen = owner ? owner->gen : liveGen[si].load();
            if (auto* c = makeIR (gen)) { if (auto* old = pendingIR[si].exchange (c)) delete old; }
            a.irKey = key;
        }
    }
}
void TapeSpaceRackProcessor::run()
{
    while (! threadShouldExit())
    {
        emptyTrash();
        if (prepared.load())
        {
            const juce::ScopedLock sl (buildLock);
            for (int s = 0; s < numSlots() && ! threadShouldExit(); ++s) buildSlot (s, true);
        }
        emptyTrash();
        wait (25);
    }
}

void TapeSpaceRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stopThread (4000);
    prepared = false;
    const juce::ScopedLock sl (buildLock);
    emptyTrash();
    for (int s = 0; s < numSlots(); ++s)
    {
        const auto si = (size_t) s;
        if (live[si]) { trashModule (live[si]); live[si] = nullptr; }
        if (auto* m = pendingMod[si].exchange (nullptr)) trashModule (m);
        emptyTrash();
        delete pendingIR[si].exchange (nullptr);
        alloc[si] = {};
        lastValid[si] = false;
    }
    hostRate = sampleRate;
    for (int s = 0; s < numSlots(); ++s) { buildSlot (s, false); if (live[(size_t) s]) liveGen[(size_t) s] = live[(size_t) s]->gen; }
    monoScratch.setSize (2, samplesPerBlock);
    setLatencySamples (0);
    prepared = true;
    startThread (juce::Thread::Priority::low);
}

void TapeSpaceRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numCh = buffer.getNumChannels();
    if (n == 0 || numCh == 0) return;
    inPeak.store (std::max (inPeak.load(), buffer.getMagnitude (0, n)));

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) { if (*b > 1) { hostBpm.store (*b); bpmFromHost.store (true); } }
    const double bpm = hostBpm.load();

    juce::AudioBuffer<float>* work = &buffer;
    if (numCh == 1)
    {
        if (monoScratch.getNumSamples() < n) monoScratch.setSize (2, n, false, false, true);
        monoScratch.copyFrom (0, 0, buffer, 0, 0, n);
        monoScratch.copyFrom (1, 0, buffer, 0, 0, n);
        work = &monoScratch;
    }
    float* L = work->getWritePointer (0); float* R = work->getWritePointer (1);

    for (int s = 0; s < numSlots(); ++s)
    {
        const auto si = (size_t) s;
        const int t = reader.type (s);
        if (auto* m = pendingMod[si].exchange (nullptr)) { trashModule (live[si]); live[si] = m; liveGen[si] = m->gen; lastValid[si] = false; }
        Module* m = live[si];
        if (m != nullptr && m->type() != t) { trashModule (m); live[si] = m = nullptr; }
        if (m == nullptr || ! reader.on (s)) continue;
        if (t == kReverb)
        {
            auto* rv = static_cast<Reverb*> (m);
            ConvPair* c = pendingIR[si].load();
            if (c != nullptr && c->gen == m->gen && pendingIR[si].compare_exchange_strong (c, nullptr))
                if (auto* d = rv->setIR (c)) toTrash (nullptr, d);
            if (auto* f = rv->takeFinished()) toTrash (nullptr, f);
        }
        const Params p = reader.params (s);
        if (! lastValid[si] || p != lastP[si]) { m->setParams (p); lastP[si] = p; lastValid[si] = true; }
        m->setBpm (bpm);
        m->process (L, R, n);
        lvIn[si].store (std::max (lvIn[si].load(), (float) m->inMeter));
        lvOut[si].store (std::max (lvOut[si].load(), (float) m->outMeter));
        lvWet[si].store (std::max (lvWet[si].load(), (float) m->wetMeter));
    }
    if (numCh == 1) buffer.copyFrom (0, 0, monoScratch, 0, 0, n);
    for (int c = 2; c < numCh; ++c) buffer.clear (c, 0, n);
    outPeak.store (std::max (outPeak.load(), buffer.getMagnitude (0, n)));

    const auto scope = specFifo.write (n);
    auto feed = [&] (int start, int size, int offset)
    {
        for (int i = 0; i < size; ++i)
        {
            float v = buffer.getSample (0, offset + i);
            if (numCh > 1) v = 0.5f * (v + buffer.getSample (1, offset + i));
            specBuffer[(size_t) (start + i)] = v;
        }
    };
    if (scope.blockSize1 > 0) feed (scope.startIndex1, scope.blockSize1, 0);
    if (scope.blockSize2 > 0) feed (scope.startIndex2, scope.blockSize2, scope.blockSize1);
}

// ----------------------------------------------------------------------------- parameter helpers
void TapeSpaceRackProcessor::setParam (juce::RangedAudioParameter* p, float normalised)
{
    if (p != nullptr && std::abs (p->getValue() - normalised) > 1e-7f) { p->beginChangeGesture(); p->setValueNotifyingHost (normalised); p->endChangeGesture(); }
}
void TapeSpaceRackProcessor::markDirty() { updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true)); }
void TapeSpaceRackProcessor::setPlain (int slot, const char* key, double value)
{
    const int k = keyIndex (key);
    if (k < 0 || ! juce::isPositiveAndBelow (slot, numSlots())) return;
    const int t = reader.type (slot);
    const int h = reader.mappedSlot (t) == slot ? hostIndex (t, k) : -1;
    if (k == 0) { editRack ([&] { store.v[(size_t) slot][0] = value; }); return; }   // module type changes the mapping
    store.v[(size_t) slot][(size_t) k] = value;
    if (h >= 0) { auto* p = hostPrm[(size_t) h]; setParam (p, p->convertTo0to1 ((float) value)); }   // host-visible: automation can be recorded
    else markDirty();
}
double TapeSpaceRackProcessor::getPlain (int slot, const char* key) const
{
    const int k = keyIndex (key);
    return k >= 0 && juce::isPositiveAndBelow (slot, numSlots()) ? reader.value (slot, k) : 0.0;
}
void TapeSpaceRackProcessor::flushHost()
{
    for (int t = 0; t < 3; ++t)
    {
        const int s = reader.mappedSlot (t); if (s < 0) continue;
        for (auto& hp : hostParams()) if (hp.type == t) { const int k = keyIndex (hp.key); store.v[(size_t) s][(size_t) k] = reader.value (s, k); }
    }
}
void TapeSpaceRackProcessor::pushHost()
{
    const auto& hps = hostParams();
    for (int t = 0; t < 3; ++t)
    {
        const int s = reader.mappedSlot (t); if (s < 0) continue;
        for (size_t i = 0; i < hps.size(); ++i)
            if (hps[i].type == t) { auto* p = hostPrm[i]; setParam (p, p->convertTo0to1 ((float) store.v[(size_t) s][(size_t) keyIndex (hps[i].key)].load())); }
    }
}
std::vector<double> TapeSpaceRackProcessor::readSlot (int slot) const
{
    std::vector<double> v;
    for (auto& x : store.v[(size_t) slot]) v.push_back (x.load());
    return v;
}
void TapeSpaceRackProcessor::writeSlot (int slot, const std::vector<double>& values)
{
    for (size_t k = 0; k < values.size() && k < store.v[(size_t) slot].size(); ++k) store.v[(size_t) slot][k] = values[k];
}
std::vector<double> TapeSpaceRackProcessor::slotValues (int tsType, const Params& p, bool on) const
{
    std::vector<double> v { (double) (tsType >= 0 ? slotTypeOf (tsType) : 0), on ? 1.0 : 0.0 };
    for (auto& sp : paramSpecs()) v.push_back (p.*(sp.field));
    return v;
}
void TapeSpaceRackProcessor::permuteUserIR (const std::function<void (std::array<UserIR, (size_t) numSlots()>&)>& fn)
{
    const juce::ScopedLock sl (irLock);
    fn (userIR);
}

// ----------------------------------------------------------------------------- rack editing
int TapeSpaceRackProcessor::usedSlots() const
{
    int n = 0;
    for (int s = 0; s < numSlots(); ++s) if (reader.slotType (s) != kEmpty) n = s + 1;
    return n;
}
bool TapeSpaceRackProcessor::addModule (int t)
{
    const int s = usedSlots();
    if (s >= numSlots()) return false;
    permuteUserIR ([s] (auto& u) { u[(size_t) s] = {}; });
    editRack ([&] { writeSlot (s, slotValues (t, defaultsFor (t))); });
    return true;
}
void TapeSpaceRackProcessor::removeSlot (int slot)
{
    const int n = usedSlots();
    if (slot < 0 || slot >= n) return;
    editRack ([&] { for (int s = slot; s < n - 1; ++s) writeSlot (s, readSlot (s + 1)); writeSlot (n - 1, slotValues (-1, Params())); });
    permuteUserIR ([slot, n] (auto& u) { for (int s = slot; s < n - 1; ++s) u[(size_t) s] = u[(size_t) s + 1]; u[(size_t) n - 1] = {}; });
}
void TapeSpaceRackProcessor::duplicateSlot (int slot)
{
    const int n = usedSlots();
    if (n >= numSlots() || slot < 0 || slot >= n) return;
    editRack ([&] { for (int s = n; s > slot + 1; --s) writeSlot (s, readSlot (s - 1)); writeSlot (slot + 1, readSlot (slot)); });
    permuteUserIR ([slot, n] (auto& u) { for (int s = n; s > slot + 1; --s) u[(size_t) s] = u[(size_t) s - 1]; u[(size_t) slot + 1] = u[(size_t) slot]; });
}
void TapeSpaceRackProcessor::moveSlot (int from, int to)
{
    const int n = usedSlots();
    if (from == to || from < 0 || to < 0 || from >= n || to >= n) return;
    editRack ([&]
    {
        const auto moving = readSlot (from);
        if (from < to) for (int s = from; s < to; ++s) writeSlot (s, readSlot (s + 1));
        else           for (int s = from; s > to; --s) writeSlot (s, readSlot (s - 1));
        writeSlot (to, moving);
    });
    permuteUserIR ([from, to] (auto& u)
    {
        const UserIR m = u[(size_t) from];
        if (from < to) for (int s = from; s < to; ++s) u[(size_t) s] = u[(size_t) s + 1];
        else           for (int s = from; s > to; --s) u[(size_t) s] = u[(size_t) s - 1];
        u[(size_t) to] = m;
    });
}

juce::String TapeSpaceRackProcessor::loadUserIR (int slot, const juce::File& file)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return juce::String::fromUTF8 ("このファイルは読み込めませんでした（WAV / AIFF / FLAC などの音声ファイルを選んでください）");
    const double fs = r->sampleRate > 0 ? r->sampleRate : 48000;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (fs * 12));
    if (n < 16) return juce::String::fromUTF8 ("IR が短すぎます");
    juce::AudioBuffer<float> b ((int) std::min<unsigned int> (2, r->numChannels), n);
    r->read (&b, 0, n, 0, true, b.getNumChannels() > 1);
    UserIR u; u.name = file.getFileName(); u.path = file.getFullPathName(); u.fs = fs; u.version = irVersionSeq++;
    for (int c = 0; c < 2; ++c) { const float* src = b.getReadPointer (std::min (c, b.getNumChannels() - 1)); u.data[(size_t) c].assign (src, src + n); }
    permuteUserIR ([slot, &u] (auto& a) { a[(size_t) slot] = u; });
    setPlain (slot, "source", 2);
    return {};
}

// ----------------------------------------------------------------------------- factory presets
void TapeSpaceRackProcessor::applyModulePreset (int slot, int index)
{
    const int t = reader.type (slot);
    if (t < 0 || ! juce::isPositiveAndBelow (index, presets.numModule (t))) return;
    Params p = defaultsFor (t);
    PresetBank::apply (p, presets.moduleValues (t, index));
    const bool on = reader.on (slot);
    editRack ([&] { writeSlot (slot, slotValues (t, p, on)); });
}
void TapeSpaceRackProcessor::applyRackPreset (int index)
{
    const auto slots = presets.rackSlots (index);
    if (! slots.isArray()) return;
    permuteUserIR ([] (auto& u) { for (auto& x : u) x = {}; });
    flushHost();
    for (int s = 0; s < numSlots(); ++s)
    {
        if (s >= slots.size()) { writeSlot (s, slotValues (-1, Params())); continue; }
        const auto& sl = slots[s];
        const int t = typeFromKey (sl["type"].toString().toStdString());
        Params p = defaultsFor (t);
        if (sl.hasProperty ("preset")) PresetBank::apply (p, presets.moduleValuesByName (t, sl["preset"].toString()));
        PresetBank::apply (p, sl["p"]);
        const bool on = ! sl.hasProperty ("on") || (bool) sl["on"];
        writeSlot (s, slotValues (t, p, on));
    }
    pushHost(); markDirty();
}
int TapeSpaceRackProcessor::getNumPrograms() { return std::max (1, presets.numRack()); }
const juce::String TapeSpaceRackProcessor::getProgramName (int index) { return presets.rackName (index); }
void TapeSpaceRackProcessor::setCurrentProgram (int index)
{
    if (index == currentProgram || ! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;
    applyRackPreset (index);
}

// ----------------------------------------------------------------------------- state (parameters + embedded user IRs)
void TapeSpaceRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    flushHost();
    auto st = state.copyState();
    st.removeChild (st.getChildWithName ("Rack"), nullptr);
    juce::ValueTree rack ("Rack");
    for (int s = 0; s < numSlots(); ++s)
    {
        juce::ValueTree t ("Slot");
        const auto& keys = slotKeys();
        for (size_t k = 0; k < keys.size(); ++k) t.setProperty (keys[k], store.v[(size_t) s][k].load(), nullptr);
        rack.appendChild (t, nullptr);
    }
    st.appendChild (rack, nullptr);
    juce::ValueTree irs ("UserIRs");
    {
        const juce::ScopedLock sl (irLock);
        for (int s = 0; s < numSlots(); ++s)
        {
            const auto& u = userIR[(size_t) s];
            if (u.empty()) continue;
            juce::MemoryOutputStream raw;
            {
                juce::GZIPCompressorOutputStream gz (raw, 6);
                const int n = (int) u.data[0].size();
                gz.writeInt (n);
                for (int c = 0; c < 2; ++c) gz.write (u.data[(size_t) c].data(), (size_t) n * sizeof (float));
            }
            juce::ValueTree t ("IR");
            t.setProperty ("slot", s, nullptr).setProperty ("name", u.name, nullptr).setProperty ("path", u.path, nullptr)
             .setProperty ("fs", u.fs, nullptr).setProperty ("data", raw.getMemoryBlock().toBase64Encoding(), nullptr);
            irs.appendChild (t, nullptr);
        }
    }
    st.removeChild (st.getChildWithName ("UserIRs"), nullptr);
    st.appendChild (irs, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}
void TapeSpaceRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (! xml || ! xml->hasTagName (state.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto irs = tree.getChildWithName ("UserIRs");
    std::array<UserIR, (size_t) numSlots()> loaded;
    for (auto t : irs)
    {
        const int s = t["slot"];
        if (! juce::isPositiveAndBelow (s, numSlots())) continue;
        juce::MemoryBlock mb; if (! mb.fromBase64Encoding (t["data"].toString())) continue;
        juce::MemoryInputStream mis (mb, false);
        juce::GZIPDecompressorInputStream gz (mis);
        const int n = gz.readInt();
        if (n <= 0 || n > 96000 * 12) continue;
        UserIR u; u.name = t["name"].toString(); u.path = t["path"].toString(); u.fs = t["fs"]; u.version = irVersionSeq++;
        bool ok = true;
        for (int c = 0; c < 2 && ok; ++c) { u.data[(size_t) c].resize ((size_t) n); ok = gz.read (u.data[(size_t) c].data(), (int) ((size_t) n * sizeof (float))) == (int) ((size_t) n * sizeof (float)); }
        if (ok) loaded[(size_t) s] = std::move (u);
    }
    tree.removeChild (irs, nullptr);
    permuteUserIR ([&loaded] (auto& u) { u = loaded; });
    const auto rack = tree.getChildWithName ("Rack");
    if (rack.isValid())
    {
        const auto& keys = slotKeys();
        for (int s = 0; s < numSlots() && s < rack.getNumChildren(); ++s)
        {
            const auto t = rack.getChild (s);
            for (size_t k = 0; k < keys.size(); ++k) if (t.hasProperty (keys[k])) store.v[(size_t) s][k] = (double) t[keys[k]];
        }
    }
    tree.removeChild (rack, nullptr);
    state.replaceState (tree);
}

juce::AudioProcessorEditor* TapeSpaceRackProcessor::createEditor() { return new TapeSpaceRackEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TapeSpaceRackProcessor(); }
