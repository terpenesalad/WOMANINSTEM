#include "ClapFormat.h"
#include "NativeWindow.h"
#include <clap/clap.h>
#include <set>
#include <mutex>

namespace wis::daw
{

namespace
{
    thread_local bool inClapAudio = false;

    /** Stable 32-bit id for a CLAP plugin id string (what JUCE uses to tell plugins in one file apart). */
    int hashId (const juce::String& id) { return (int) (juce::uint32) id.hashCode64(); }

    struct ClapModule
    {
        juce::DynamicLibrary lib;
        const clap_plugin_entry_t* entry = nullptr;
        juce::File file;

        ~ClapModule()
        {
            if (entry != nullptr) entry->deinit();
        }

        static std::shared_ptr<ClapModule> open (const juce::File& f, juce::String& error)
        {
            static std::mutex mutex;
            static std::map<juce::String, std::weak_ptr<ClapModule>> openModules;
            const std::lock_guard<std::mutex> lg (mutex);
            if (auto existing = openModules[f.getFullPathName()].lock())
                return existing;

            auto m = std::make_shared<ClapModule>();
            m->file = f;
            if (! m->lib.open (f.getFullPathName()))
            {
                error = "Couldn't load " + f.getFileName();
                return {};
            }
            auto* e = (const clap_plugin_entry_t*) m->lib.getFunction ("clap_entry");
            if (e == nullptr || ! clap_version_is_compatible (e->clap_version))
            {
                error = f.getFileName() + " isn't a compatible CLAP plugin";
                return {};
            }
            if (! e->init (f.getFullPathName().toRawUTF8()))
            {
                error = f.getFileName() + " failed to initialise";
                return {};
            }
            m->entry = e;
            openModules[f.getFullPathName()] = m;
            return m;
        }

        const clap_plugin_factory_t* factory() const
        {
            return entry != nullptr ? (const clap_plugin_factory_t*) entry->get_factory (CLAP_PLUGIN_FACTORY_ID) : nullptr;
        }
    };

    bool hasFeature (const clap_plugin_descriptor_t* d, const char* feature)
    {
        if (d == nullptr || d->features == nullptr) return false;
        for (auto f = d->features; *f != nullptr; ++f)
            if (std::strcmp (*f, feature) == 0) return true;
        return false;
    }

    juce::String str (const char* s) { return s != nullptr ? juce::String::fromUTF8 (s) : juce::String(); }
}

class ClapInstance;

namespace
{
    /** Instances that are alive (main-thread callbacks may arrive after an instance is gone). */
    std::mutex liveMutex;
    std::set<const void*> liveInstances;
    bool isLive (const void* p) { const std::lock_guard<std::mutex> lg (liveMutex); return liveInstances.count (p) > 0; }
}

// =====================================================================================================
//  Parameters
// =====================================================================================================
class ClapParameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    ClapParameter (ClapInstance& o, const clap_param_info_t& info, double value)
        : owner (o), id (info.id), cookie (info.cookie), flags (info.flags), name (str (info.name)),
          minValue (info.min_value), maxValue (info.max_value), defaultPlain (info.default_value)
    {
        normalised = (float) toNormalised (value);
    }

    double toPlain (float v) const       { return minValue + juce::jlimit (0.0f, 1.0f, v) * (maxValue - minValue); }
    double toNormalised (double plain) const { return maxValue > minValue ? (plain - minValue) / (maxValue - minValue) : 0.0; }

    float getValue() const override;
    void setValue (float v) override;
    float getDefaultValue() const override { return (float) toNormalised (defaultPlain); }
    juce::String getName (int maxLen) const override { return name.substring (0, maxLen); }
    juce::String getLabel() const override { return {}; }
    juce::String getText (float v, int maxLen) const override;
    float getValueForText (const juce::String& t) const override;
    int getNumSteps() const override
    {
        if ((flags & CLAP_PARAM_IS_STEPPED) != 0) return juce::jmax (2, (int) std::round (maxValue - minValue) + 1);
        return AudioProcessorParameter::getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override       { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
    bool isBoolean() const override        { return isDiscrete() && std::abs (maxValue - minValue - 1.0) < 1.0e-9; }
    bool isAutomatable() const override    { return (flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
    juce::String getParameterID() const override { return juce::String ((juce::int64) id); }

    /** The plugin changed it (from its GUI or internally). */
    void updateFromPlugin (double plain)
    {
        normalised = (float) toNormalised (plain);
        sendValueChangedMessageToListeners (normalised.load());
    }

    ClapInstance& owner;
    const clap_id id;
    void* cookie;
    const juce::uint32 flags;
    const juce::String name;
    const double minValue, maxValue, defaultPlain;
    std::atomic<float> normalised { 0.0f };
};

// =====================================================================================================
//  Instance
// =====================================================================================================
class ClapInstance final : public juce::AudioPluginInstance
{
public:
    static BusesProperties busesFor (const clap_plugin_t* plugin, bool instrument)
    {
        BusesProperties b;
        auto* ports = (const clap_plugin_audio_ports_t*) plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS);
        auto channelSet = [] (uint32_t n)
        {
            return n == 1 ? juce::AudioChannelSet::mono() : n == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::discreteChannels ((int) n);
        };
        if (ports == nullptr)
        {
            if (! instrument) b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
            return b.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
        }
        for (int input = 1; input >= 0; --input)
        {
            const uint32_t count = ports->count (plugin, input == 1);
            for (uint32_t i = 0; i < count; ++i)
            {
                clap_audio_port_info_t info {};
                if (! ports->get (plugin, i, input == 1, &info) || info.channel_count == 0) continue;
                const auto name = str (info.name).isNotEmpty() ? str (info.name) : juce::String (input ? "Input " : "Output ") + juce::String ((int) i + 1);
                if (input == 1) b = b.withInput (name, channelSet (info.channel_count), true);
                else            b = b.withOutput (name, channelSet (info.channel_count), true);
            }
        }
        return b;
    }

    ClapInstance (std::shared_ptr<ClapModule> m, const juce::PluginDescription& d, const clap_host_t* h, const clap_plugin_t* p)
        : AudioPluginInstance (busesFor (p, d.isInstrument)), module (std::move (m)), desc (d), plugin (p)
    {
        params = (const clap_plugin_params_t*) plugin->get_extension (plugin, CLAP_EXT_PARAMS);
        stateExt = (const clap_plugin_state_t*) plugin->get_extension (plugin, CLAP_EXT_STATE);
        latencyExt = (const clap_plugin_latency_t*) plugin->get_extension (plugin, CLAP_EXT_LATENCY);
        gui = (const clap_plugin_gui_t*) plugin->get_extension (plugin, CLAP_EXT_GUI);
        audioPorts = (const clap_plugin_audio_ports_t*) plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS);
        juce::ignoreUnused (h);

        // note dialect
        if (auto* np = (const clap_plugin_note_ports_t*) plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS))
        {
            hasNoteInput = np->count (plugin, true) > 0;
            clap_note_port_info_t info {};
            if (hasNoteInput && np->get (plugin, 0, true, &info))
            {
                useMidiDialect = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) == 0 && (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0;
                canTakeMidi = (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0;
            }
        }

        if (params != nullptr)
        {
            const uint32_t n = params->count (plugin);
            for (uint32_t i = 0; i < n; ++i)
            {
                clap_param_info_t info {};
                if (! params->get_info (plugin, i, &info)) continue;
                if ((info.flags & CLAP_PARAM_IS_HIDDEN) != 0) continue;
                double v = info.default_value;
                params->get_value (plugin, info.id, &v);
                auto* param = new ClapParameter (*this, info, v);
                byId[info.id] = param;
                addHostedParameter (std::unique_ptr<HostedParameter> (param));
            }
        }
        if (latencyExt != nullptr) setLatencySamples ((int) latencyExt->get (plugin));

        // event storage (never reallocated on the audio thread)
        notes.resize (maxEvents);
        midis.resize (maxEvents);
        paramEvents.resize (maxEvents);
        order.reserve ((size_t) maxEvents * 2);
        pendingParams.resize (maxEvents);
        inEvents.ctx = this;
        inEvents.size = [] (const clap_input_events_t* l) -> uint32_t { return (uint32_t) static_cast<ClapInstance*> (l->ctx)->order.size(); };
        inEvents.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t*
        {
            auto& o = static_cast<ClapInstance*> (l->ctx)->order;
            return i < o.size() ? o[i] : nullptr;
        };
        outEvents.ctx = this;
        outEvents.try_push = [] (const clap_output_events_t* l, const clap_event_header_t* e) -> bool
        {
            static_cast<ClapInstance*> (l->ctx)->receive (e);
            return true;
        };
        const std::lock_guard<std::mutex> lg (liveMutex);
        liveInstances.insert (this);
    }

    ~ClapInstance() override
    {
        {
            const std::lock_guard<std::mutex> lg (liveMutex);
            liveInstances.erase (this);
        }
        releaseResources();
        plugin->destroy (plugin);
    }

    // ---- description ----
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
    const juce::String getName() const override { return desc.name; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override  { return hasNoteInput || desc.isInstrument; }
    bool producesMidi() const override { return false; }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l == getBusesLayout(); }

    // ---- processing ----
    void prepareToPlay (double sr, int block) override
    {
        releaseResources();
        sampleRate = sr;
        maxFrames = juce::jmax (1, block);
        scratch.setSize (juce::jmax (2, getTotalNumOutputChannels() + getTotalNumInputChannels()), maxFrames);
        if (plugin->activate (plugin, sr, 1, (uint32_t) maxFrames))
        {
            active = true;
            if (latencyExt != nullptr) setLatencySamples ((int) latencyExt->get (plugin));
        }
    }

    void releaseResources() override
    {
        if (processing) { plugin->stop_processing (plugin); processing = false; }
        if (active) { plugin->deactivate (plugin); active = false; }
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();
        if (! active || n > maxFrames) { buffer.clear(); return; }
        inClapAudio = true;
        if (! processing) processing = plugin->start_processing (plugin);

        buildEvents (midi);
        midi.clear();
        outgoingMidi = &midi;
        fillTransport();

        // audio ports -> channels of the JUCE buffer (inputs first channels, outputs overwrite them)
        const int numInBuses = getBusCount (true), numOutBuses = getBusCount (false);
        inBuffers.resize ((size_t) numInBuses);
        outBuffers.resize ((size_t) numOutBuses);
        inPtrs.resize ((size_t) numInBuses);
        outPtrs.resize ((size_t) numOutBuses);
        int scratchChannel = 0;
        for (int b = 0; b < numInBuses; ++b)
        {
            auto* bus = getBus (true, b);
            const int chans = bus->getNumberOfChannels();
            const int first = bus->getChannelIndexInProcessBlockBuffer (0);
            auto& ptrs = inPtrs[(size_t) b];
            ptrs.resize ((size_t) chans);
            for (int c = 0; c < chans; ++c)
            {
                // copy inputs: some plugins write their outputs before reading all inputs
                auto* dst = scratch.getWritePointer (juce::jmin (scratch.getNumChannels() - 1, scratchChannel++));
                if (first + c < buffer.getNumChannels()) juce::FloatVectorOperations::copy (dst, buffer.getReadPointer (first + c), n);
                else juce::FloatVectorOperations::clear (dst, n);
                ptrs[(size_t) c] = dst;
            }
            inBuffers[(size_t) b] = { ptrs.data(), nullptr, (uint32_t) chans, 0, 0 };
        }
        for (int b = 0; b < numOutBuses; ++b)
        {
            auto* bus = getBus (false, b);
            const int chans = bus->getNumberOfChannels();
            const int first = bus->getChannelIndexInProcessBlockBuffer (0);
            auto& ptrs = outPtrs[(size_t) b];
            ptrs.resize ((size_t) chans);
            for (int c = 0; c < chans; ++c)
            {
                float* dst = first + c < buffer.getNumChannels() ? buffer.getWritePointer (first + c)
                                                                 : scratch.getWritePointer (juce::jmin (scratch.getNumChannels() - 1, scratchChannel++));
                juce::FloatVectorOperations::clear (dst, n);
                ptrs[(size_t) c] = dst;
            }
            outBuffers[(size_t) b] = { ptrs.data(), nullptr, (uint32_t) chans, 0, 0 };
        }

        clap_process_t p {};
        p.steady_time = steadyTime;
        p.frames_count = (uint32_t) n;
        p.transport = &transport;
        p.audio_inputs = inBuffers.data();
        p.audio_inputs_count = (uint32_t) numInBuses;
        p.audio_outputs = outBuffers.data();
        p.audio_outputs_count = (uint32_t) numOutBuses;
        p.in_events = &inEvents;
        p.out_events = &outEvents;
        plugin->process (plugin, &p);
        steadyTime += n;

        // channels that aren't outputs are silent
        for (int c = getTotalNumOutputChannels(); c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, n);
        outgoingMidi = nullptr;
        inClapAudio = false;
    }

    // ---- state ----
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (stateExt == nullptr) return;
        juce::MemoryOutputStream out (dest, false);
        clap_ostream_t os { &out, [] (const clap_ostream_t* s, const void* buf, uint64_t size) -> int64_t
        {
            static_cast<juce::MemoryOutputStream*> (s->ctx)->write (buf, (size_t) size);
            return (int64_t) size;
        } };
        stateExt->save (plugin, &os);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (stateExt == nullptr || size <= 0) return;
        juce::MemoryInputStream in (data, (size_t) size, false);
        clap_istream_t is { &in, [] (const clap_istream_t* s, void* buf, uint64_t len) -> int64_t
        {
            return (int64_t) static_cast<juce::MemoryInputStream*> (s->ctx)->read (buf, (int) juce::jmin ((uint64_t) 1 << 30, len));
        } };
        stateExt->load (plugin, &is);
        refreshParamValues();
    }

    // ---- programs (CLAP has presets, not programs) ----
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // ---- editor ----
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasNativeEditor() const
    {
       #if JUCE_WINDOWS
        return gui != nullptr && gui->is_api_supported (plugin, CLAP_WINDOW_API_WIN32, false);
       #else
        return false;
       #endif
    }

    // ---- called by the host struct ----
    void queueParam (clap_id id, double plain)
    {
        const juce::SpinLock::ScopedLockType sl (paramLock);
        if (numPending < (int) pendingParams.size()) pendingParams[(size_t) numPending++] = { id, plain };
    }
    void refreshParamValues()
    {
        if (params == nullptr) return;
        for (auto& [id, p] : byId)
        {
            double v = 0;
            if (params->get_value (plugin, id, &v)) p->updateFromPlugin (v);
        }
    }
    void latencyChanged() { if (latencyExt != nullptr) setLatencySamples ((int) latencyExt->get (plugin)); }
    void restart()
    {
        if (! active) return;
        const auto sr = sampleRate; const int frames = maxFrames;
        suspendProcessing (true);
        releaseResources();
        prepareToPlay (sr, frames);
        suspendProcessing (false);
    }

    std::shared_ptr<ClapModule> module;
    juce::PluginDescription desc;
    const clap_plugin_t* plugin;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* stateExt = nullptr;
    const clap_plugin_latency_t* latencyExt = nullptr;
    const clap_plugin_gui_t* gui = nullptr;
    const clap_plugin_audio_ports_t* audioPorts = nullptr;
    std::function<void (int, int)> onEditorResize;
    std::unique_ptr<clap_host_t> hostStruct;   // owned here so it outlives the plugin
    std::map<clap_id, ClapParameter*> byId;

private:
    void buildEvents (const juce::MidiBuffer& midi)
    {
        order.clear();
        int nn = 0, nm = 0, np = 0;
        {
            const juce::SpinLock::ScopedLockType sl (paramLock);
            for (int i = 0; i < numPending && np < maxEvents; ++i)
            {
                auto& e = paramEvents[(size_t) np++];
                e = {};
                e.header = { (uint32_t) sizeof (clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id = pendingParams[(size_t) i].first;
                if (auto it = byId.find (e.param_id); it != byId.end()) e.cookie = it->second->cookie;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = pendingParams[(size_t) i].second;
                order.push_back (&e.header);
            }
            numPending = 0;
        }
        if (! hasNoteInput) return;
        for (const auto meta : midi)
        {
            const auto* d = meta.data;
            if (meta.numBytes < 1 || meta.numBytes > 3) continue;
            const auto t = (uint32_t) juce::jmax (0, meta.samplePosition);
            const int type = d[0] & 0xf0;
            const bool noteOn = type == 0x90 && meta.numBytes == 3 && d[2] > 0;
            const bool noteOff = type == 0x80 || (type == 0x90 && meta.numBytes == 3 && d[2] == 0);
            if ((noteOn || noteOff) && ! useMidiDialect && nn < maxEvents)
            {
                auto& e = notes[(size_t) nn++];
                e = {};
                e.header = { (uint32_t) sizeof (clap_event_note_t), t, CLAP_CORE_EVENT_SPACE_ID, (uint16_t) (noteOn ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0 };
                e.note_id = -1;
                e.port_index = 0;
                e.channel = (int16_t) (d[0] & 0x0f);
                e.key = (int16_t) d[1];
                e.velocity = noteOn ? d[2] / 127.0 : 0.0;
                order.push_back (&e.header);
            }
            else if ((useMidiDialect || canTakeMidi) && nm < maxEvents)
            {
                auto& e = midis[(size_t) nm++];
                e = {};
                e.header = { (uint32_t) sizeof (clap_event_midi_t), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 };
                e.port_index = 0;
                for (int i = 0; i < meta.numBytes; ++i) e.data[i] = d[i];
                order.push_back (&e.header);
            }
        }
        std::stable_sort (order.begin(), order.end(), [] (auto* a, auto* b) { return a->time < b->time; });
    }

    void receive (const clap_event_header_t* e)
    {
        if (e == nullptr || e->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        if (e->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto* pv = reinterpret_cast<const clap_event_param_value_t*> (e);
            if (auto it = byId.find (pv->param_id); it != byId.end()) it->second->updateFromPlugin (pv->value);
        }
        else if (e->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || e->type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            auto* g = reinterpret_cast<const clap_event_param_gesture_t*> (e);
            if (auto it = byId.find (g->param_id); it != byId.end())
            {
                if (e->type == CLAP_EVENT_PARAM_GESTURE_BEGIN) it->second->beginChangeGesture();
                else it->second->endChangeGesture();
            }
        }
        else if (e->type == CLAP_EVENT_MIDI && outgoingMidi != nullptr)
        {
            auto* m = reinterpret_cast<const clap_event_midi_t*> (e);
            const int len = juce::MidiMessage::getMessageLengthFromFirstByte (m->data[0]);
            outgoingMidi->addEvent (m->data, len, (int) e->time);
        }
        else if ((e->type == CLAP_EVENT_NOTE_ON || e->type == CLAP_EVENT_NOTE_OFF) && outgoingMidi != nullptr)
        {
            auto* nv = reinterpret_cast<const clap_event_note_t*> (e);
            const int ch = juce::jlimit (1, 16, nv->channel + 1);
            const auto msg = e->type == CLAP_EVENT_NOTE_ON ? juce::MidiMessage::noteOn (ch, juce::jlimit (0, 127, (int) nv->key), (float) nv->velocity)
                                                           : juce::MidiMessage::noteOff (ch, juce::jlimit (0, 127, (int) nv->key));
            outgoingMidi->addEvent (msg, (int) e->time);
        }
    }

    void fillTransport()
    {
        transport = {};
        transport.header = { (uint32_t) sizeof (clap_event_transport_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
        transport.tempo = 120.0;
        transport.tsig_num = 4;
        transport.tsig_denom = 4;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) transport.tempo = *b;
                if (auto ppq = pos->getPpqPosition()) transport.song_pos_beats = (clap_beattime) std::llround (*ppq * (double) CLAP_BEATTIME_FACTOR);
                if (auto sec = pos->getTimeInSeconds()) transport.song_pos_seconds = (clap_sectime) std::llround (*sec * (double) CLAP_SECTIME_FACTOR);
                if (auto bar = pos->getPpqPositionOfLastBarStart()) transport.bar_start = (clap_beattime) std::llround (*bar * (double) CLAP_BEATTIME_FACTOR);
                if (auto bc = pos->getBarCount()) transport.bar_number = (int32_t) *bc;
                if (auto ts = pos->getTimeSignature()) { transport.tsig_num = (uint16_t) ts->numerator; transport.tsig_denom = (uint16_t) ts->denominator; }
                if (pos->getIsPlaying()) transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
                if (pos->getIsRecording()) transport.flags |= CLAP_TRANSPORT_IS_RECORDING;
                if (pos->getIsLooping())
                    if (auto loop = pos->getLoopPoints())
                    {
                        transport.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;
                        transport.loop_start_beats = (clap_beattime) std::llround (loop->ppqStart * (double) CLAP_BEATTIME_FACTOR);
                        transport.loop_end_beats = (clap_beattime) std::llround (loop->ppqEnd * (double) CLAP_BEATTIME_FACTOR);
                    }
            }
    }

    static constexpr int maxEvents = 1024;
    double sampleRate = 48000.0;
    int maxFrames = 512;
    bool active = false, processing = false;
    bool hasNoteInput = false, useMidiDialect = false, canTakeMidi = false;
    juce::int64 steadyTime = 0;
    juce::AudioBuffer<float> scratch;
    std::vector<clap_audio_buffer_t> inBuffers, outBuffers;
    std::vector<std::vector<float*>> inPtrs, outPtrs;
    std::vector<clap_event_note_t> notes;
    std::vector<clap_event_midi_t> midis;
    std::vector<clap_event_param_value_t> paramEvents;
    std::vector<const clap_event_header_t*> order;
    clap_input_events_t inEvents {};
    clap_output_events_t outEvents {};
    clap_event_transport_t transport {};
    juce::MidiBuffer* outgoingMidi = nullptr;
    juce::SpinLock paramLock;
    std::vector<std::pair<clap_id, double>> pendingParams;
    int numPending = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapInstance)
};

float ClapParameter::getValue() const
{
    // on the main thread we can ask the plugin (read-only meters etc. never send change events)
    if (owner.params != nullptr && juce::MessageManager::existsAndIsCurrentThread())
    {
        double plain = 0;
        if (owner.params->get_value (owner.plugin, id, &plain))
            const_cast<ClapParameter*> (this)->normalised = (float) toNormalised (plain);
    }
    return normalised.load();
}

void ClapParameter::setValue (float v)
{
    normalised = juce::jlimit (0.0f, 1.0f, v);
    owner.queueParam (id, toPlain (normalised.load()));
}

juce::String ClapParameter::getText (float v, int maxLen) const
{
    char buf[256] = {};
    if (owner.params != nullptr && owner.params->value_to_text (owner.plugin, id, toPlain (v), buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf).substring (0, maxLen);
    return juce::String (toPlain (v), 2).substring (0, maxLen);
}

float ClapParameter::getValueForText (const juce::String& t) const
{
    double plain = 0;
    if (owner.params != nullptr && owner.params->text_to_value (owner.plugin, id, t.toRawUTF8(), &plain))
        return (float) toNormalised (plain);
    return (float) toNormalised (t.getDoubleValue());
}

// ---- editor --------------------------------------------------------------------------------------------

class ClapNativeEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ClapNativeEditor (ClapInstance& p) : AudioProcessorEditor (p), owner (p)
    {
        addAndMakeVisible (window);
        auto* plugin = owner.plugin;
        created = owner.gui->create (plugin, CLAP_WINDOW_API_WIN32, false);
        if (created)
        {
            owner.gui->set_scale (plugin, NativeChildWindow::displayScale());
            uint32_t w = 600, h = 400;
            owner.gui->get_size (plugin, &w, &h);
            clap_window_t cw {};
            cw.api = CLAP_WINDOW_API_WIN32;
            cw.win32 = window.getNativeHandle();
            owner.gui->set_parent (plugin, &cw);
            applyPhysicalSize ((int) w, (int) h);
            owner.gui->show (plugin);
        }
        else
        {
            setSize (400, 120);
        }
        owner.onEditorResize = [sp = juce::Component::SafePointer<ClapNativeEditor> (this)] (int w, int h)
        {
            juce::MessageManager::callAsync ([sp, w, h] { if (sp != nullptr) sp->applyPhysicalSize (w, h); });
        };
    }

    ~ClapNativeEditor() override
    {
        owner.onEditorResize = nullptr;
        if (created) owner.gui->destroy (owner.plugin);
        window.destroy();
    }

    void applyPhysicalSize (int w, int h)
    {
        const double scale = NativeChildWindow::displayScale();
        setSize (juce::jmax (100, juce::roundToInt (w / scale)), juce::jmax (60, juce::roundToInt (h / scale)));
    }

    void resized() override { window.setBounds (getLocalBounds()); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        if (! created) { g.setColour (juce::Colours::white); g.drawText ("This plugin's editor couldn't be opened.", getLocalBounds(), juce::Justification::centred); }
    }

private:
    ClapInstance& owner;
    NativeChildWindow window;
    bool created = false;
};

juce::AudioProcessorEditor* ClapInstance::createEditor()
{
    if (hasNativeEditor())
        return new ClapNativeEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

// =====================================================================================================
//  The host side handed to each plugin
// =====================================================================================================
namespace
{
    ClapInstance* instanceOf (const clap_host_t* h) { return h != nullptr ? static_cast<ClapInstance*> (h->host_data) : nullptr; }

    const clap_host_log_t hostLog { [] (const clap_host_t*, clap_log_severity severity, const char* msg)
    {
        if (severity >= CLAP_LOG_WARNING) DBG ("CLAP: " << (msg != nullptr ? msg : ""));
    } };

    const clap_host_thread_check_t hostThreadCheck {
        [] (const clap_host_t*) { return juce::MessageManager::existsAndIsCurrentThread(); },
        [] (const clap_host_t*) { return inClapAudio; }
    };

    const clap_host_params_t hostParams {
        [] (const clap_host_t* h, clap_param_rescan_flags) { if (auto* i = instanceOf (h)) i->refreshParamValues(); },
        [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
        [] (const clap_host_t*) {}   // we process continuously, so flushes happen in process()
    };

    const clap_host_state_t hostState { [] (const clap_host_t* h) { if (auto* i = instanceOf (h)) i->updateHostDisplay(); } };

    const clap_host_latency_t hostLatency { [] (const clap_host_t* h) { if (auto* i = instanceOf (h)) i->latencyChanged(); } };

    const clap_host_gui_t hostGui {
        [] (const clap_host_t*) {},
        [] (const clap_host_t* h, uint32_t w, uint32_t hh) { if (auto* i = instanceOf (h)) if (i->onEditorResize) { i->onEditorResize ((int) w, (int) hh); return true; } return false; },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*, bool) {}
    };

    const clap_host_audio_ports_t hostAudioPorts {
        [] (const clap_host_t*, uint32_t) { return false; },
        [] (const clap_host_t*, uint32_t) {}
    };

    const clap_host_note_ports_t hostNotePorts {
        [] (const clap_host_t*) -> uint32_t { return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; },
        [] (const clap_host_t*, uint32_t) {}
    };

    std::unique_ptr<clap_host_t> makeHost()
    {
        auto h = std::make_unique<clap_host_t>();
        *h = {};
        h->clap_version = CLAP_VERSION;
        h->host_data = nullptr;
        h->name = "WOMANINSTEM";
        h->vendor = "WOMANINSTEM";
        h->url = "https://github.com/terpenesalad/WOMANINSTEM";
        h->version = "3.0";
        h->get_extension = [] (const clap_host_t*, const char* id) -> const void*
        {
            if (std::strcmp (id, CLAP_EXT_LOG) == 0)          return &hostLog;
            if (std::strcmp (id, CLAP_EXT_THREAD_CHECK) == 0) return &hostThreadCheck;
            if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)       return &hostParams;
            if (std::strcmp (id, CLAP_EXT_STATE) == 0)        return &hostState;
            if (std::strcmp (id, CLAP_EXT_LATENCY) == 0)      return &hostLatency;
            if (std::strcmp (id, CLAP_EXT_GUI) == 0)          return &hostGui;
            if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)  return &hostAudioPorts;
            if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)   return &hostNotePorts;
            return nullptr;
        };
        h->request_restart = [] (const clap_host_t* host)
        {
            juce::MessageManager::callAsync ([data = host->host_data] { if (data != nullptr && isLive (data)) static_cast<ClapInstance*> (data)->restart(); });
        };
        h->request_process = [] (const clap_host_t*) {};
        h->request_callback = [] (const clap_host_t* host)
        {
            juce::MessageManager::callAsync ([data = host->host_data]
            {
                if (data != nullptr && isLive (data)) { auto* i = static_cast<ClapInstance*> (data); i->plugin->on_main_thread (i->plugin); }
            });
        };
        return h;
    }
}

// =====================================================================================================
//  Format
// =====================================================================================================
void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier)
{
    if (! fileMightContainThisPluginType (fileOrIdentifier)) return;
    const juce::File f (fileOrIdentifier);
    juce::String error;
    auto module = ClapModule::open (f, error);
    if (module == nullptr) return;
    auto* factory = module->factory();
    if (factory == nullptr) return;

    const uint32_t count = factory->get_plugin_count (factory);
    for (uint32_t i = 0; i < count; ++i)
    {
        auto* d = factory->get_plugin_descriptor (factory, i);
        if (d == nullptr || d->id == nullptr) continue;
        auto desc = std::make_unique<juce::PluginDescription>();
        desc->pluginFormatName = formatName();
        desc->fileOrIdentifier = f.getFullPathName();
        desc->lastFileModTime = f.getLastModificationTime();
        desc->lastInfoUpdateTime = juce::Time::getCurrentTime();
        desc->name = str (d->name);
        desc->descriptiveName = str (d->description).isNotEmpty() ? str (d->description) : desc->name;
        desc->manufacturerName = str (d->vendor);
        desc->version = str (d->version);
        desc->uniqueId = desc->deprecatedUid = hashId (str (d->id));
        desc->isInstrument = hasFeature (d, CLAP_PLUGIN_FEATURE_INSTRUMENT);
        desc->category = desc->isInstrument ? "Instrument" : hasFeature (d, CLAP_PLUGIN_FEATURE_NOTE_EFFECT) ? "Note Effect" : "Effect";
        desc->hasSharedContainer = count > 1;

        // channel counts need a live instance
        auto host = makeHost();
        if (auto* p = factory->create_plugin (factory, host.get(), d->id))
        {
            if (p->init (p))
            {
                if (auto* ports = (const clap_plugin_audio_ports_t*) p->get_extension (p, CLAP_EXT_AUDIO_PORTS))
                {
                    for (int input = 0; input < 2; ++input)
                    {
                        clap_audio_port_info_t info {};
                        if (ports->count (p, input == 1) > 0 && ports->get (p, 0, input == 1, &info))
                            (input == 1 ? desc->numInputChannels : desc->numOutputChannels) = (int) info.channel_count;
                    }
                }
            }
            p->destroy (p);
        }
        results.add (desc.release());
    }
}

std::unique_ptr<juce::AudioPluginInstance> ClapPluginFormat::create (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
    auto module = ClapModule::open (juce::File (d.fileOrIdentifier), error);
    if (module == nullptr) return {};
    auto* factory = module->factory();
    if (factory == nullptr) { error = "No plugins in " + d.fileOrIdentifier; return {}; }

    const char* id = nullptr;
    for (uint32_t i = 0; i < factory->get_plugin_count (factory); ++i)
        if (auto* desc = factory->get_plugin_descriptor (factory, i); desc != nullptr && hashId (str (desc->id)) == d.uniqueId)
            id = desc->id;
    if (id == nullptr) { error = d.name + " is no longer in " + juce::File (d.fileOrIdentifier).getFileName(); return {}; }

    auto host = makeHost();
    auto* p = factory->create_plugin (factory, host.get(), id);
    if (p == nullptr || ! p->init (p))
    {
        if (p != nullptr) p->destroy (p);
        error = "Couldn't start " + d.name;
        return {};
    }
    auto inst = std::make_unique<ClapInstance> (module, d, host.get(), p);
    host->host_data = inst.get();
    inst->hostStruct = std::move (host);
    inst->setRateAndBufferSizeDetails (sr, block);
    return inst;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& d, double sr, int block, PluginCreationCallback callback)
{
    juce::String error;
    auto inst = create (d, sr, block, error);
    callback (std::move (inst), error);
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
    return f.hasFileExtension (".clap") && (f.existsAsFile() || f.isDirectory());
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& dirs, bool recursive, bool)
{
    juce::StringArray results;
    for (int i = 0; i < dirs.getNumPaths(); ++i)
    {
        juce::File d (dirs.getRawString (i));
        if (! d.isDirectory()) continue;
        for (const auto& f : d.findChildFiles (juce::File::findFiles, recursive, "*.clap"))
            results.addIfNotAlreadyThere (f.getFullPathName());
    }
    return results;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;
   #if JUCE_WINDOWS
    paths.add (juce::File (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getFullPathName() + "\\Common Files\\CLAP"));
    paths.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs").getChildFile ("Common").getChildFile ("CLAP"));
   #else
    paths.add (juce::File ("~/.clap"));
    paths.add (juce::File ("/usr/lib/clap"));
   #endif
    for (auto p : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}), juce::File::getSeparatorChar() == '\\' ? ";" : ":", {}))
        if (p.isNotEmpty()) paths.add (juce::File (p));
    paths.removeRedundantPaths();
    return paths;
}

} // namespace wis::daw
