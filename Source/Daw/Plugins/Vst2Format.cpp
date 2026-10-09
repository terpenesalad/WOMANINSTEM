#include "Vst2Format.h"
#include "Vst2Abi.h"
#include "NativeWindow.h"
#include "Vst2Bridge.h"
#include "Separation/ModelManager.h"

namespace wis::daw
{

using namespace wis::vst2;

namespace
{
    thread_local int32_t shellIdToLoad = 0;     // asked for by shell plugins through hostCurrentId
    thread_local bool inAudioCallback = false;

    /** A loaded plugin binary, shared by every instance from the same file. */
    struct Module
    {
        juce::DynamicLibrary lib;
        EntryProc entry = nullptr;
        juce::File file;

        static std::shared_ptr<Module> open (const juce::File& f, juce::String& error)
        {
            static std::mutex mutex;
            static std::map<juce::String, std::weak_ptr<Module>> openModules;
            const std::lock_guard<std::mutex> lg (mutex);

            if (auto existing = openModules[f.getFullPathName()].lock())
                return existing;

            auto m = std::make_shared<Module>();
            m->file = f;
            if (! m->lib.open (f.getFullPathName()))
            {
                error = "Couldn't load " + f.getFileName();
                return {};
            }
            m->entry = (EntryProc) m->lib.getFunction ("VSTPluginMain");
            if (m->entry == nullptr) m->entry = (EntryProc) m->lib.getFunction ("main");
            if (m->entry == nullptr)
            {
                error = f.getFileName() + " isn't a VST2 plugin";
                return {};
            }
            openModules[f.getFullPathName()] = m;
            return m;
        }
    };

    juce::String getString (Effect* e, int32_t opcode, int32_t index = 0)
    {
        char buf[512] = {};
        e->dispatcher (e, opcode, index, 0, buf, 0.0f);
        buf[sizeof (buf) - 1] = 0;
        return juce::String::fromUTF8 (buf).trim();
    }

    intptr_t WIS_VST_CALL hostCallback (Effect*, int32_t, int32_t, intptr_t, void*, float);

    Effect* instantiate (Module& m, int32_t shellId, juce::String& error)
    {
        shellIdToLoad = shellId;
        Effect* e = nullptr;
        try { e = m.entry (hostCallback); }
        catch (...) { e = nullptr; }
        shellIdToLoad = 0;
        if (e == nullptr || e->magic != effectMagic)
        {
            error = m.file.getFileName() + " didn't create a VST2 plugin";
            return nullptr;
        }
        e->hostReserved1 = 0;
        e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
        return e;
    }

    bool isSynth (Effect* e)
    {
        const auto cat = (int32_t) e->dispatcher (e, effGetPlugCategory, 0, 0, nullptr, 0.0f);
        return (e->flags & flagIsSynth) != 0 || cat == categorySynth;
    }

    void describe (Effect* e, const juce::File& f, juce::PluginDescription& d)
    {
        d.pluginFormatName = Vst2PluginFormat::formatName();
        d.fileOrIdentifier = f.getFullPathName();
        d.lastFileModTime = f.getLastModificationTime();
        d.lastInfoUpdateTime = juce::Time::getCurrentTime();
        d.name = getString (e, effGetEffectName);
        if (d.name.isEmpty()) d.name = getString (e, effGetProductString);
        if (d.name.isEmpty()) d.name = f.getFileNameWithoutExtension();
        d.descriptiveName = d.name;
        d.manufacturerName = getString (e, effGetVendorString);
        const auto v = (int) e->dispatcher (e, effGetVendorVersion, 0, 0, nullptr, 0.0f);
        d.version = v > 0 ? juce::String (v / 1000) + "." + juce::String ((v / 100) % 10) + "." + juce::String (v % 100) : juce::String();
        d.uniqueId = d.deprecatedUid = e->uniqueId;
        d.isInstrument = isSynth (e);
        d.category = d.isInstrument ? "Synth" : "Effect";
        d.numInputChannels = e->numInputs;
        d.numOutputChannels = e->numOutputs;
        d.hasSharedContainer = false;
    }
}

// =====================================================================================================
//  Parameters
// =====================================================================================================
class Vst2Instance;

class Vst2Parameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    Vst2Parameter (Effect* e, int i, juce::String n, float def, bool automatable)
        : effect (e), index (i), name (std::move (n)), defaultValue (def), canAutomate (automatable) {}

    float getValue() const override             { return effect->getParameter (effect, index); }
    void setValue (float v) override            { effect->setParameter (effect, index, juce::jlimit (0.0f, 1.0f, v)); }
    float getDefaultValue() const override      { return defaultValue; }
    juce::String getName (int maxLen) const override { return name.substring (0, maxLen); }
    juce::String getLabel() const override      { return getString (effect, effGetParamLabel, index); }
    bool isAutomatable() const override         { return canAutomate; }
    juce::String getParameterID() const override { return juce::String (index); }

    juce::String getText (float v, int maxLen) const override
    {
        if (std::abs (v - getValue()) < 1.0e-5f)
        {
            auto t = getString (effect, effGetParamDisplay, index);
            auto label = getLabel();
            if (t.isNotEmpty()) return (label.isNotEmpty() ? t + " " + label : t).substring (0, maxLen);
        }
        return juce::String (v, 2);
    }
    float getValueForText (const juce::String& text) const override { return juce::jlimit (0.0f, 1.0f, text.getFloatValue()); }

private:
    Effect* effect;
    int index;
    juce::String name;
    float defaultValue;
    bool canAutomate;
};

// =====================================================================================================
//  Instance
// =====================================================================================================
class Vst2Instance final : public juce::AudioPluginInstance
{
public:
    Vst2Instance (std::shared_ptr<Module> m, Effect* e, const juce::PluginDescription& d)
        : AudioPluginInstance (busesFor (e->numInputs, e->numOutputs)), module (std::move (m)), effect (e), desc (d)
    {
        effect->hostReserved1 = (intptr_t) this;
        for (int i = 0; i < effect->numParams; ++i)
        {
            auto n = getString (effect, effGetParamName, i);
            if (n.isEmpty()) n = "Param " + juce::String (i + 1);
            addHostedParameter (std::make_unique<Vst2Parameter> (effect, i, n, effect->getParameter (effect, i), true));
        }
        setLatencySamples (effect->initialDelay);
        midiEvents.resize (maxEvents);
        eventBlock.resize (sizeof (Events) + sizeof (Event*) * (size_t) maxEvents);
        timeInfo = {};
    }

    ~Vst2Instance() override
    {
        if (active) suspend();
        effect->dispatcher (effect, effClose, 0, 0, nullptr, 0.0f);
        effect = nullptr;
    }

    static BusesProperties busesFor (int ins, int outs)
    {
        BusesProperties b;
        if (ins > 0) b = b.withInput ("Input", ins >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        if (ins > 2) b = b.withInput ("Sidechain", juce::AudioChannelSet::discreteChannels (ins - 2), true);
        if (outs > 0) b = b.withOutput ("Output", outs >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        if (outs > 2) b = b.withOutput ("Aux", juce::AudioChannelSet::discreteChannels (outs - 2), true);
        return b;
    }

    // ---- description ----
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
    const juce::String getName() const override { return desc.name; }
    double getTailLengthSeconds() const override
    {
        const auto t = effect->dispatcher (effect, effGetTailSize, 0, 0, nullptr, 0.0f);
        return t > 1 ? (double) t / juce::jmax (1.0, getSampleRate()) : 0.0;
    }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l == getBusesLayout(); }

    // ---- processing ----
    void prepareToPlay (double sr, int block) override
    {
        if (active) suspend();
        sampleRate = sr;
        blockSize = juce::jmax (1, block);
        effect->dispatcher (effect, effSetSampleRate, 0, 0, nullptr, (float) sr);
        effect->dispatcher (effect, effSetBlockSize, 0, blockSize, nullptr, 0.0f);
        effect->dispatcher (effect, effSetProcessPrecision, 0, 0, nullptr, 0.0f);   // 32-bit float
        inputs.setSize (juce::jmax (1, effect->numInputs), blockSize);
        outputs.setSize (juce::jmax (1, effect->numOutputs), blockSize);
        inPtrs.assign ((size_t) juce::jmax (1, effect->numInputs), nullptr);
        outPtrs.assign ((size_t) juce::jmax (1, effect->numOutputs), nullptr);
        effect->dispatcher (effect, effMainsChanged, 0, 1, nullptr, 0.0f);
        effect->dispatcher (effect, effStartProcess, 0, 0, nullptr, 0.0f);
        active = true;
        setLatencySamples (effect->initialDelay);
    }

    void releaseResources() override
    {
        if (active) suspend();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();
        if (! active || n > blockSize)
        {
            buffer.clear();
            return;
        }
        inAudioCallback = true;
        updateTimeInfo();

        // MIDI in
        auto* ev = reinterpret_cast<Events*> (eventBlock.data());
        int count = 0;
        for (const auto meta : midi)
        {
            if (count >= maxEvents) break;
            const auto* raw = meta.data;
            if (meta.numBytes > 3 || raw[0] == 0xF0) continue;   // short messages only
            auto& me = midiEvents[(size_t) count];
            me = {};
            me.type = eventTypeMidi;
            me.byteSize = (int32_t) sizeof (MidiEvent);
            me.deltaFrames = juce::jlimit (0, n - 1, meta.samplePosition);
            me.flags = 1;
            for (int i = 0; i < meta.numBytes; ++i) me.midiData[i] = (char) raw[i];
            ev->events[count] = reinterpret_cast<Event*> (&me);
            ++count;
        }
        ev->numEvents = count;
        ev->reserved = 0;
        if (count > 0) effect->dispatcher (effect, effProcessEvents, 0, 0, ev, 0.0f);
        midi.clear();
        outgoing = &midi;

        // audio: the plugin gets separate input and output buffers
        const int nIn = effect->numInputs, nOut = effect->numOutputs;
        for (int c = 0; c < nIn; ++c)
        {
            if (c < buffer.getNumChannels()) inputs.copyFrom (c, 0, buffer, c, 0, n);
            else inputs.clear (c, 0, n);
            inPtrs[(size_t) c] = inputs.getWritePointer (c);
        }
        for (int c = 0; c < nOut; ++c)
        {
            outputs.clear (c, 0, n);
            outPtrs[(size_t) c] = outputs.getWritePointer (c);
        }
        if ((effect->flags & flagCanReplacing) != 0 && effect->processReplacing != nullptr)
            effect->processReplacing (effect, inPtrs.data(), outPtrs.data(), n);
        else if (effect->processAccumulating != nullptr)
            effect->processAccumulating (effect, inPtrs.data(), outPtrs.data(), n);

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            if (c < nOut) buffer.copyFrom (c, 0, outputs, c, 0, n);
            else buffer.clear (c, 0, n);
        }
        outgoing = nullptr;
        inAudioCallback = false;
    }

    // ---- programs ----
    int getNumPrograms() override        { return juce::jmax (1, effect->numPrograms); }
    int getCurrentProgram() override     { return (int) effect->dispatcher (effect, effGetProgram, 0, 0, nullptr, 0.0f); }
    void setCurrentProgram (int i) override
    {
        if (effect->numPrograms <= 0) return;
        effect->dispatcher (effect, effBeginSetProgram, 0, 0, nullptr, 0.0f);
        effect->dispatcher (effect, effSetProgram, 0, juce::jlimit (0, effect->numPrograms - 1, i), nullptr, 0.0f);
        effect->dispatcher (effect, effEndSetProgram, 0, 0, nullptr, 0.0f);
    }
    const juce::String getProgramName (int i) override
    {
        char buf[256] = {};
        if (effect->dispatcher (effect, effGetProgramNameIndexed, i, -1, buf, 0.0f) != 0)
            return juce::String::fromUTF8 (buf).trim();
        if (i == getCurrentProgram()) return getString (effect, effGetProgramName);
        return "Program " + juce::String (i + 1);
    }
    void changeProgramName (int i, const juce::String& name) override
    {
        if (i == getCurrentProgram())
        {
            char buf[256] = {};
            name.copyToUTF8 (buf, 24);
            effect->dispatcher (effect, effSetProgramName, 0, 0, buf, 0.0f);
        }
    }

    // ---- state ----
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream out (dest, false);
        if ((effect->flags & flagProgramChunks) != 0)
        {
            void* data = nullptr;
            const auto size = effect->dispatcher (effect, effGetChunk, 0, 0, &data, 0.0f);
            out.writeInt (0x57564332);   // 'WVC2': chunk
            out.writeInt ((int) size);
            if (data != nullptr && size > 0) out.write (data, (size_t) size);
        }
        else
        {
            out.writeInt (0x57565032);   // 'WVP2': parameter values
            out.writeInt (getCurrentProgram());
            out.writeInt (effect->numParams);
            for (int i = 0; i < effect->numParams; ++i) out.writeFloat (effect->getParameter (effect, i));
        }
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        const int magic = in.readInt();
        if (magic == 0x57564332)
        {
            const int len = in.readInt();
            if (len > 0 && len <= size - 8)
            {
                juce::MemoryBlock chunk ((const char*) data + 8, (size_t) len);
                effect->dispatcher (effect, effSetChunk, 0, len, chunk.getData(), 0.0f);
            }
        }
        else if (magic == 0x57565032)
        {
            const int program = in.readInt();
            const int count = in.readInt();
            if (effect->numPrograms > 0) setCurrentProgram (program);
            for (int i = 0; i < count && i < effect->numParams && ! in.isExhausted(); ++i)
                effect->setParameter (effect, i, in.readFloat());
        }
    }

    // ---- editor ----
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasNativeEditor() const { return (effect->flags & flagHasEditor) != 0 && NativeChildWindow::isSupported(); }

    // ---- host callback ----
    intptr_t callback (int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);

    Effect* effect;
    juce::Component::SafePointer<juce::AudioProcessorEditor> nativeEditor;
    std::function<void (int, int)> onEditorResize;   // set by the native editor

private:
    void suspend()
    {
        effect->dispatcher (effect, effStopProcess, 0, 0, nullptr, 0.0f);
        effect->dispatcher (effect, effMainsChanged, 0, 0, nullptr, 0.0f);
        active = false;
    }

    void updateTimeInfo()
    {
        timeInfo = {};
        timeInfo.sampleRate = sampleRate;
        timeInfo.tempo = 120.0;
        timeInfo.timeSigNumerator = 4;
        timeInfo.timeSigDenominator = 4;
        timeInfo.flags = timeTempoValid | timeTimeSigValid | timePpqPosValid | timeBarsValid;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto s = pos->getTimeInSamples()) timeInfo.samplePos = (double) *s;
                if (auto b = pos->getBpm()) timeInfo.tempo = *b;
                if (auto ppq = pos->getPpqPosition()) timeInfo.ppqPos = *ppq;
                if (auto bar = pos->getPpqPositionOfLastBarStart()) timeInfo.barStartPos = *bar;
                if (auto ts = pos->getTimeSignature()) { timeInfo.timeSigNumerator = ts->numerator; timeInfo.timeSigDenominator = ts->denominator; }
                if (pos->getIsPlaying()) timeInfo.flags |= timeTransportPlaying;
                if (pos->getIsRecording()) timeInfo.flags |= timeTransportRecording;
                if (pos->getIsLooping())
                    if (auto loop = pos->getLoopPoints())
                    {
                        timeInfo.flags |= timeTransportCycleActive | timeCyclePosValid;
                        timeInfo.cycleStartPos = loop->ppqStart;
                        timeInfo.cycleEndPos = loop->ppqEnd;
                    }
                if (pos->getIsPlaying() != wasPlaying) timeInfo.flags |= timeTransportChanged;
                wasPlaying = pos->getIsPlaying();
            }
    }

    static constexpr int maxEvents = 1024;
    std::shared_ptr<Module> module;
    juce::PluginDescription desc;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool active = false, wasPlaying = false;
    juce::AudioBuffer<float> inputs, outputs;
    std::vector<float*> inPtrs, outPtrs;
    std::vector<MidiEvent> midiEvents;
    std::vector<char> eventBlock;
    juce::MidiBuffer* outgoing = nullptr;
    TimeInfo timeInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vst2Instance)
};

// ---- editors ------------------------------------------------------------------------------------------

/** The plugin's own editor, drawn into a native child window. */
class Vst2NativeEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Vst2NativeEditor (Vst2Instance& p) : AudioProcessorEditor (p), owner (p)
    {
        addAndMakeVisible (window);
        owner.effect->dispatcher (owner.effect, effEditOpen, 0, 0, window.getNativeHandle(), 0.0f);
        Rect* r = nullptr;
        owner.effect->dispatcher (owner.effect, effEditGetRect, 0, 0, &r, 0.0f);
        int w = 480, h = 320;
        if (r != nullptr && r->right > r->left && r->bottom > r->top) { w = r->right - r->left; h = r->bottom - r->top; }
        applyPhysicalSize (w, h);
        owner.onEditorResize = [sp = juce::Component::SafePointer<Vst2NativeEditor> (this)] (int nw, int nh)
        {
            juce::MessageManager::callAsync ([sp, nw, nh] { if (sp != nullptr) sp->applyPhysicalSize (nw, nh); });
        };
        startTimerHz (30);
    }

    ~Vst2NativeEditor() override
    {
        stopTimer();
        owner.onEditorResize = nullptr;
        owner.effect->dispatcher (owner.effect, effEditClose, 0, 0, nullptr, 0.0f);
        window.destroy();
    }

    void applyPhysicalSize (int w, int h)
    {
        const double scale = NativeChildWindow::displayScale();
        setSize (juce::jmax (100, juce::roundToInt (w / scale)), juce::jmax (60, juce::roundToInt (h / scale)));
    }

    void resized() override { window.setBounds (getLocalBounds()); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

private:
    void timerCallback() override { owner.effect->dispatcher (owner.effect, effEditIdle, 0, 0, nullptr, 0.0f); }

    Vst2Instance& owner;
    NativeChildWindow window;
};

juce::AudioProcessorEditor* Vst2Instance::createEditor()
{
    if (hasNativeEditor())
        return new Vst2NativeEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

intptr_t Vst2Instance::callback (int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
    switch (opcode)
    {
        case hostAutomate:
            if (auto* p = getParameters()[index]) p->sendValueChangedMessageToListeners (opt);
            return 0;
        case hostBeginEdit:
            if (auto* p = getParameters()[index]) p->beginChangeGesture();
            return 0;
        case hostEndEdit:
            if (auto* p = getParameters()[index]) p->endChangeGesture();
            return 0;
        case hostGetTime:
            return (intptr_t) &timeInfo;
        case hostProcessEvents:
            if (outgoing != nullptr && ptr != nullptr)
            {
                auto* ev = static_cast<Events*> (ptr);
                for (int i = 0; i < ev->numEvents; ++i)
                    if (ev->events[i] != nullptr && ev->events[i]->type == eventTypeMidi)
                    {
                        auto* me = reinterpret_cast<MidiEvent*> (ev->events[i]);
                        const auto status = (juce::uint8) me->midiData[0];
                        const int len = juce::MidiMessage::getMessageLengthFromFirstByte (status);
                        outgoing->addEvent (me->midiData, len, juce::jmax (0, (int) me->deltaFrames));
                    }
            }
            return 1;
        case hostIOChanged:
            setLatencySamples (effect->initialDelay);
            return 1;
        case hostSizeWindow:
            if (onEditorResize) onEditorResize (index, (int) value);
            return 1;
        case hostGetSampleRate:  return (intptr_t) sampleRate;
        case hostGetBlockSize:   return blockSize;
        case hostUpdateDisplay:  updateHostDisplay(); return 1;
        default:                 return 0;
    }
}

namespace
{
    intptr_t WIS_VST_CALL hostCallback (Effect* e, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
    {
        switch (opcode)
        {
            case hostVersion:       return 2400;
            case hostCurrentId:     return shellIdToLoad != 0 ? shellIdToLoad : (e != nullptr ? e->uniqueId : 0);
            case hostIdle:          return 0;
            case hostGetCurrentProcessLevel: return inAudioCallback ? processLevelRealtime : processLevelUser;
            case hostGetAutomationState:     return 1;
            case hostGetVendorString:  if (ptr) std::strcpy (static_cast<char*> (ptr), "WOMANINSTEM"); return 1;
            case hostGetProductString: if (ptr) std::strcpy (static_cast<char*> (ptr), "WOMANINSTEM Studio"); return 1;
            case hostGetVendorVersion: return 3000;
            case hostGetLanguage:      return 1;
            case hostCanDo:
            {
                if (ptr == nullptr) return 0;
                const juce::String what (static_cast<const char*> (ptr));
                for (auto* yes : { "sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "receiveVstEvents", "receiveVstMidiEvent",
                                   "sizeWindow", "startStopProcess", "supportShell", "shellCategory" })
                    if (what == yes) return 1;
                return 0;
            }
            default: break;
        }
        if (e != nullptr && e->hostReserved1 != 0)
            return reinterpret_cast<Vst2Instance*> (e->hostReserved1)->callback (opcode, index, value, ptr, opt);
        if (opcode == hostGetSampleRate) return 48000;
        if (opcode == hostGetBlockSize) return 512;
        return 0;
    }
}

// =====================================================================================================
//  Format
// =====================================================================================================
void Vst2PluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
    if (! fileMightContainThisPluginType (fileOrIdentifier)) return;
    juce::String error;
   #if JUCE_WINDOWS && JUCE_64BIT
    if (vst2bridge::isWin32Dll (f))
    {
        // an old 32-bit plugin: it runs in the 32-bit bridge process
        auto d = std::make_unique<juce::PluginDescription>();
        if (vst2bridge::describe (f, *d, error)) results.add (d.release());
        return;
    }
   #endif
    auto module = Module::open (f, error);
    if (module == nullptr) return;

    auto* e = instantiate (*module, 0, error);
    if (e == nullptr) return;

    const auto cat = (int32_t) e->dispatcher (e, effGetPlugCategory, 0, 0, nullptr, 0.0f);
    if (cat == categoryShell)
    {
        // a shell holds several plugins: enumerate their ids, then open each one to describe it
        std::vector<std::pair<int32_t, juce::String>> ids;
        for (int guard = 0; guard < 2000; ++guard)
        {
            char name[256] = {};
            const auto id = (int32_t) e->dispatcher (e, effShellGetNextPlugin, 0, 0, name, 0.0f);
            if (id == 0) break;
            ids.push_back ({ id, juce::String::fromUTF8 (name) });
        }
        e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
        for (auto& [id, name] : ids)
        {
            auto* sub = instantiate (*module, id, error);
            if (sub == nullptr) continue;
            auto d = std::make_unique<juce::PluginDescription>();
            describe (sub, f, *d);
            if (name.isNotEmpty()) d->name = d->descriptiveName = name;
            d->uniqueId = d->deprecatedUid = id;
            d->hasSharedContainer = true;
            sub->dispatcher (sub, effClose, 0, 0, nullptr, 0.0f);
            results.add (d.release());
        }
        return;
    }

    auto d = std::make_unique<juce::PluginDescription>();
    describe (e, f, *d);
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    results.add (d.release());
}

std::unique_ptr<juce::AudioPluginInstance> Vst2PluginFormat::create (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
   #if JUCE_WINDOWS && JUCE_64BIT
    if (vst2bridge::isWin32Dll (juce::File (d.fileOrIdentifier)))
        return vst2bridge::create (d, sr, block, error);
   #endif
    auto module = Module::open (juce::File (d.fileOrIdentifier), error);
    if (module == nullptr) return {};
    auto* e = instantiate (*module, d.hasSharedContainer ? d.uniqueId : 0, error);
    if (e == nullptr) return {};
    auto inst = std::make_unique<Vst2Instance> (module, e, d);
    inst->setRateAndBufferSizeDetails (sr, block);
    return inst;
}

void Vst2PluginFormat::createPluginInstance (const juce::PluginDescription& d, double sr, int block, PluginCreationCallback callback)
{
    juce::String error;
    auto inst = create (d, sr, block, error);
    callback (std::move (inst), error);
}

bool Vst2PluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
   #if JUCE_WINDOWS
    return f.existsAsFile() && f.hasFileExtension (".dll");
   #elif JUCE_LINUX || JUCE_BSD
    return f.existsAsFile() && f.hasFileExtension (".so");
   #else
    return false;
   #endif
}

juce::String Vst2PluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool Vst2PluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool Vst2PluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).existsAsFile();
}

juce::StringArray Vst2PluginFormat::searchPathsForPlugins (const juce::FileSearchPath& dirs, bool recursive, bool)
{
    juce::StringArray results;
    for (int i = 0; i < dirs.getNumPaths(); ++i)
    {
        const auto dir = dirs.getRawString (i);
        juce::File d (juce::File::isAbsolutePath (dir) ? dir : juce::File::getCurrentWorkingDirectory().getChildFile (dir).getFullPathName());
        if (! d.isDirectory()) continue;
       #if JUCE_WINDOWS
        const char* pattern = "*.dll";
       #else
        const char* pattern = "*.so";
       #endif
        for (const auto& f : d.findChildFiles (juce::File::findFiles, recursive, pattern))
            results.addIfNotAlreadyThere (f.getFullPathName());
    }
    return results;
}

juce::FileSearchPath Vst2PluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;
   #if JUCE_WINDOWS
    const auto programFiles = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getFullPathName();
    for (auto sub : { "\\VSTPlugins", "\\Steinberg\\VSTPlugins", "\\Common Files\\VST2", "\\Common Files\\Steinberg\\VST2" })
        paths.add (juce::File (programFiles + sub));
    const auto registered = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\Software\\VST\\VSTPluginsPath");
    if (registered.isNotEmpty()) paths.add (juce::File (registered));
    paths.add (ModelManager::appDataDirectory().getChildFile ("Plugins"));   // WOMANINSTEM's own plugins folder
   #else
    for (auto p : { "~/.vst", "~/.lxvst", "/usr/lib/vst", "/usr/local/lib/vst", "/usr/lib/lxvst", "/usr/local/lib/lxvst" })
        paths.add (juce::File (p));
   #endif
    paths.removeRedundantPaths();
    return paths;
}

} // namespace wis::daw
