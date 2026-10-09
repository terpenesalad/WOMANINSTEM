#include "Vst2Bridge.h"
#include "Vst2Abi.h"
#include "BridgeProtocol.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace wis::daw::vst2bridge
{

namespace br = wis::bridge;
using namespace wis::vst2;

bool isWin32Dll (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk() || in.getTotalLength() < 0x100) return false;
    char mz[2] = {};
    if (in.read (mz, 2) != 2 || mz[0] != 'M' || mz[1] != 'Z') return false;
    in.setPosition (0x3C);
    const auto peOffset = (juce::int64) (juce::uint32) in.readInt();
    if (peOffset <= 0 || peOffset + 6 > in.getTotalLength()) return false;
    in.setPosition (peOffset);
    char sig[4] = {};
    if (in.read (sig, 4) != 4 || sig[0] != 'P' || sig[1] != 'E' || sig[2] != 0 || sig[3] != 0) return false;
    const auto machine = (juce::uint16) in.readShort();
    return machine == 0x014c;   // IMAGE_FILE_MACHINE_I386
}

juce::File bridgeExecutable()
{
    const juce::File env (juce::SystemStats::getEnvironmentVariable ("WIS_BRIDGE32", {}));
    if (env.existsAsFile()) return env;
    auto f = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("wisbridge32.exe");
    return f.existsAsFile() ? f : juce::File();
}

bool isAvailable()
{
   #if JUCE_WINDOWS && JUCE_64BIT
    return bridgeExecutable().existsAsFile();
   #else
    return false;
   #endif
}

#if JUCE_WINDOWS && JUCE_64BIT

// =====================================================================================================
//  Connection: shared memory, events and the bridge process
// =====================================================================================================
namespace
{
    std::atomic<int> connectionCounter { 0 };

    class Connection
    {
    public:
        ~Connection() { shutdown(); }

        bool start (const juce::File& dll, juce::String& error)
        {
            const auto exe = bridgeExecutable();
            if (! exe.existsAsFile())
            {
                error = dll.getFileName() + " is a 32-bit plugin, and the 32-bit bridge (wisbridge32.exe) is missing. Reinstall WOMANINSTEM.";
                return false;
            }
            base = "Local\\WisBridge-" + juce::String ((int) GetCurrentProcessId()) + "-" + juce::String (++connectionCounter)
                   + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt());

            mapping = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (br::Shared), base.toRawUTF8());
            if (mapping == nullptr) { error = "Couldn't create shared memory for the bridge"; return false; }
            shm = static_cast<br::Shared*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (br::Shared)));
            if (shm == nullptr) { error = "Couldn't map shared memory for the bridge"; return false; }
            std::memset (shm, 0, offsetof (br::Shared, data) + 1024);
            shm->version = br::protocolVersion;

            auto makeEvent = [this] (const char* suffix)
            {
                char name[300];
                br::objectName (name, sizeof (name), base.toRawUTF8(), suffix);
                return CreateEventA (nullptr, FALSE, FALSE, name);
            };
            ctlReq = makeEvent (".ctlReq");
            ctlRep = makeEvent (".ctlRep");
            audReq = makeEvent (".audReq");
            audRep = makeEvent (".audRep");
            if (ctlReq == nullptr || ctlRep == nullptr || audReq == nullptr || audRep == nullptr) { error = "Couldn't create bridge events"; return false; }

            const auto cmd = "\"" + exe.getFullPathName() + "\" --host " + juce::String ((int) GetCurrentProcessId())
                             + " --shm \"" + base + "\" --dll \"" + dll.getFullPathName() + "\"";
            STARTUPINFOW si {};
            si.cb = sizeof (si);
            PROCESS_INFORMATION pi {};
            const std::wstring wide (cmd.toWideCharPointer());
            std::vector<wchar_t> cmdLine (wide.begin(), wide.end());
            cmdLine.push_back (0);
            if (! CreateProcessW (nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                  dll.getParentDirectory().getFullPathName().toWideCharPointer(), &si, &pi))
            {
                error = "Couldn't start the 32-bit bridge (error " + juce::String ((int) GetLastError()) + ")";
                return false;
            }
            process = pi.hProcess;
            pid = pi.dwProcessId;
            CloseHandle (pi.hThread);
            return true;
        }

        bool alive() const
        {
            return process != nullptr && WaitForSingleObject (process, 0) == WAIT_TIMEOUT && ! dead.load();
        }

        /** One control call. Returns false if the bridge didn't answer (crashed / hung). */
        bool control (br::Op op, int32_t opcode = 0, int32_t index = 0, int64_t value = 0, float opt = 0.0f, br::PtrKind kind = br::ptrNone,
                      const void* in = nullptr, int inLength = 0, int timeoutMs = 15000)
        {
            const juce::ScopedLock sl (ctlLock);
            if (! alive()) return false;
            shm->ctlOp = op;
            shm->ctlOpcode = opcode;
            shm->ctlIndex = index;
            shm->ctlValue = value;
            shm->ctlOpt = opt;
            shm->ctlKind = kind;
            shm->ctlResult = 0;
            shm->dataLength = 0;
            if (in != nullptr && inLength > 0)
            {
                const int n = juce::jmin (inLength, br::dataSize - 1);
                std::memcpy (shm->data, in, (size_t) n);
                shm->data[n] = 0;
                shm->dataLength = n;
            }
            const int32_t seq = ++ctlSeq;
            shm->ctlSeq = seq;
            if (op == br::opOpenEditor || op == br::opShowEditor) AllowSetForegroundWindow (pid);
            SetEvent (ctlReq);
            return waitFor (ctlRep, [this, seq] { return shm->ctlDoneSeq == seq; }, timeoutMs);
        }

        juce::String dataString() const { return juce::String::fromUTF8 (shm->data, juce::jlimit (0, 4096, (int) std::strlen (shm->data))); }

        template <typename Done>
        bool waitFor (HANDLE ev, Done done, int timeoutMs)
        {
            HANDLE waits[] = { ev, process };
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
            for (;;)
            {
                if (done()) return true;
                const auto now = juce::Time::getMillisecondCounter();
                if (now >= deadline) return false;
                const DWORD r = WaitForMultipleObjects (2, waits, FALSE, deadline - now);
                if (r == WAIT_OBJECT_0 + 1) { dead = true; return done(); }
                if (r == WAIT_TIMEOUT) return done();
            }
        }

        void shutdown()
        {
            if (process != nullptr)
            {
                if (alive()) control (br::opQuit, 0, 0, 0, 0.0f, br::ptrNone, nullptr, 0, 3000);
                if (WaitForSingleObject (process, 3000) == WAIT_TIMEOUT) TerminateProcess (process, 1);
                CloseHandle (process);
                process = nullptr;
            }
            for (auto* h : { &ctlReq, &ctlRep, &audReq, &audRep })
                if (*h != nullptr) { CloseHandle (*h); *h = nullptr; }
            if (shm != nullptr) { UnmapViewOfFile (shm); shm = nullptr; }
            if (mapping != nullptr) { CloseHandle (mapping); mapping = nullptr; }
        }

        br::Shared* shm = nullptr;
        HANDLE audReq = nullptr, audRep = nullptr;
        std::atomic<bool> dead { false };
        DWORD pid = 0;

    private:
        juce::String base;
        HANDLE mapping = nullptr, ctlReq = nullptr, ctlRep = nullptr, process = nullptr;
        juce::CriticalSection ctlLock;
        int32_t ctlSeq = 0;
    };

    bool startAndInit (Connection& c, const juce::File& dll, br::Info& info, juce::String& error)
    {
        if (! c.start (dll, error)) return false;
        if (! c.control (br::opInit, 0, 0, 0, 0.0f, br::ptrNone, nullptr, 0, 30000))
        {
            error = dll.getFileName() + ": the 32-bit plugin didn't start (it may have crashed while loading)";
            return false;
        }
        if (c.shm->ctlResult != 1)
        {
            error = dll.getFileName() + ": " + c.dataString();
            return false;
        }
        std::memcpy (&info, c.shm->data, sizeof (info));
        info.name[sizeof (info.name) - 1] = info.vendor[sizeof (info.vendor) - 1] = info.product[sizeof (info.product) - 1] = 0;
        return true;
    }

    void fillDescription (const br::Info& info, const juce::File& f, juce::PluginDescription& d)
    {
        d.pluginFormatName = "VST";
        d.fileOrIdentifier = f.getFullPathName();
        d.lastFileModTime = f.getLastModificationTime();
        d.lastInfoUpdateTime = juce::Time::getCurrentTime();
        d.name = juce::String::fromUTF8 (info.name).trim();
        if (d.name.isEmpty()) d.name = juce::String::fromUTF8 (info.product).trim();
        if (d.name.isEmpty()) d.name = f.getFileNameWithoutExtension();
        d.descriptiveName = d.name + " (32-bit, bridged)";
        d.manufacturerName = juce::String::fromUTF8 (info.vendor).trim();
        const int v = info.vendorVersion;
        d.version = v > 0 ? juce::String (v / 1000) + "." + juce::String ((v / 100) % 10) + "." + juce::String (v % 100) : juce::String();
        d.uniqueId = d.deprecatedUid = info.uniqueId;
        d.isInstrument = info.isSynth != 0;
        d.category = d.isInstrument ? "Synth" : "Effect";
        d.numInputChannels = info.numInputs;
        d.numOutputChannels = info.numOutputs;
        d.hasSharedContainer = false;
    }
}

// =====================================================================================================
//  The bridged instance
// =====================================================================================================
class BridgedInstance;

class BridgedParameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    BridgedParameter (BridgedInstance& o, int i, juce::String n, juce::String l, float def)
        : owner (o), index (i), name (std::move (n)), label (std::move (l)), defaultValue (def) {}

    float getValue() const override;
    void setValue (float v) override;
    float getDefaultValue() const override       { return defaultValue; }
    juce::String getName (int maxLen) const override { return name.substring (0, maxLen); }
    juce::String getLabel() const override       { return label; }
    juce::String getParameterID() const override { return juce::String (index); }
    juce::String getText (float v, int maxLen) const override;
    float getValueForText (const juce::String& text) const override { return juce::jlimit (0.0f, 1.0f, text.getFloatValue()); }

private:
    BridgedInstance& owner;
    int index;
    juce::String name, label;
    float defaultValue;
};

class BridgedInstance final : public juce::AudioPluginInstance
{
public:
    static BusesProperties busesFor (int ins, int outs)
    {
        BusesProperties b;
        if (ins > 0) b = b.withInput ("Input", ins >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        if (ins > 2) b = b.withInput ("Sidechain", juce::AudioChannelSet::discreteChannels (juce::jmin (ins, br::maxChannels) - 2), true);
        if (outs > 0) b = b.withOutput ("Output", outs >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        if (outs > 2) b = b.withOutput ("Aux", juce::AudioChannelSet::discreteChannels (juce::jmin (outs, br::maxChannels) - 2), true);
        return b;
    }

    BridgedInstance (std::unique_ptr<Connection> c, const br::Info& i, const juce::PluginDescription& d)
        : AudioPluginInstance (busesFor (i.numInputs, i.numOutputs)), conn (std::move (c)), info (i), desc (d)
    {
        const int np = juce::jmin (info.numParams, br::maxParams);
        values = std::vector<std::atomic<float>> ((size_t) np);
        dirty = std::vector<std::atomic<bool>> ((size_t) np);
        refreshValues();
        for (int p = 0; p < np; ++p)
        {
            auto n = dispatchString (effGetParamName, p);
            if (n.isEmpty()) n = "Param " + juce::String (p + 1);
            addHostedParameter (std::make_unique<BridgedParameter> (*this, p, n, dispatchString (effGetParamLabel, p), values[(size_t) p].load()));
        }
        lastLatency = info.initialDelay;
        setLatencySamples (lastLatency);
    }

    ~BridgedInstance() override
    {
        if (editorOpen) closeEditor();
        conn.reset();
    }

    // ---- helpers used by parameters / editor ----
    juce::String dispatchString (int32_t opcode, int32_t index = 0, int64_t value = 0)
    {
        if (! conn->control (br::opDispatch, opcode, index, value, 0.0f, br::ptrStringOut)) return {};
        return conn->dataString().trim();
    }
    int64_t dispatchValue (int32_t opcode, int32_t index = 0, int64_t value = 0, float opt = 0.0f)
    {
        return conn->control (br::opDispatch, opcode, index, value, opt) ? conn->shm->ctlResult : 0;
    }
    void refreshValues()
    {
        if (values.empty() || ! conn->control (br::opGetAllParams)) return;
        const int n = juce::jmin ((int) values.size(), (int) conn->shm->ctlResult);
        const auto* f = reinterpret_cast<const float*> (conn->shm->data);
        for (int p = 0; p < n; ++p) values[(size_t) p] = f[p];
    }
    float cachedValue (int p) const { return juce::isPositiveAndBelow (p, (int) values.size()) ? values[(size_t) p].load() : 0.0f; }
    void setCachedValue (int p, float v)
    {
        if (! juce::isPositiveAndBelow (p, (int) values.size())) return;
        values[(size_t) p] = v;
        dirty[(size_t) p] = true;
        anyDirty = true;
        if (! active)   // nothing is processing: send it now
            conn->control (br::opSetParam, 0, p, 0, v);
    }
    bool isAlive() const { return conn->alive(); }
    bool isEditorShowing() const { return conn->alive() && conn->shm->editorOpen != 0; }
    bool openEditor()  { editorOpen = conn->control (br::opOpenEditor) && conn->shm->ctlResult == 1; return editorOpen; }
    void showEditor()  { conn->control (br::opShowEditor); }
    void closeEditor() { conn->control (br::opCloseEditor); editorOpen = false; }
    bool pluginHasEditor() const { return info.hasEditor != 0; }

    // ---- description ----
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
    const juce::String getName() const override { return desc.name; }
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l == getBusesLayout(); }

    // ---- processing ----
    void prepareToPlay (double sr, int block) override
    {
        sampleRate = sr;
        stale = false;
        conn->control (br::opPrepare, 0, juce::jlimit (1, br::maxFrames, block), 0, (float) sr);
        active = true;
    }

    void releaseResources() override
    {
        if (active) conn->control (br::opSuspend);
        active = false;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int total = buffer.getNumSamples();
        auto& s = *conn->shm;
        if (! active || ! conn->alive())
        {
            buffer.clear();
            midi.clear();
            return;
        }
        if (stale)
        {
            // the bridge is still busy with a block that timed out: wait for it to catch up, silently
            if (s.audDoneSeq != audSeq) { buffer.clear(); midi.clear(); return; }
            stale = false;
        }

        updateTime (total);
        juce::MidiBuffer midiOut;
        const int nIn = juce::jmin (info.numInputs, br::maxChannels), nOut = juce::jmin (info.numOutputs, br::maxChannels);
        const bool offline = isNonRealtime();

        for (int start = 0; start < total; start += br::maxFrames)
        {
            const int n = juce::jmin (br::maxFrames, total - start);
            s.numFrames = n;
            s.numIn = nIn;
            s.numOut = nOut;
            s.samplePos = timeSamplePos + start;
            s.sampleRate = sampleRate;
            s.ppqPos = timePpq + (timeTempo / 60.0) * (double) start / juce::jmax (1.0, sampleRate);
            s.tempo = timeTempo;
            s.barStartPos = timeBarStart;
            s.cycleStartPos = timeCycleStart;
            s.cycleEndPos = timeCycleEnd;
            s.timeSigNum = timeSigNum;
            s.timeSigDen = timeSigDen;
            s.timeFlags = timeFlags;
            s.nonRealtime = offline ? 1 : 0;

            // MIDI for this chunk
            int m = 0;
            for (const auto meta : midi)
            {
                if (meta.samplePosition < start || meta.samplePosition >= start + n) continue;
                if (meta.numBytes > 3 || meta.data[0] == 0xF0 || m >= br::maxMidi) continue;
                auto& e = s.midiIn[m++];
                e.delta = meta.samplePosition - start;
                std::memset (e.bytes, 0, 4);
                std::memcpy (e.bytes, meta.data, (size_t) meta.numBytes);
            }
            s.numMidiIn = m;

            // parameters the host changed
            int sets = 0;
            if (anyDirty.exchange (false))
                for (size_t p = 0; p < dirty.size(); ++p)
                    if (dirty[p].exchange (false))
                    {
                        if (sets >= br::maxParamSets) { dirty[p] = true; anyDirty = true; continue; }
                        s.paramSets[sets++] = { (int32_t) p, values[p].load() };
                    }
            s.numParamSets = sets;

            for (int c = 0; c < nIn; ++c)
            {
                float* d = s.audioIn + c * br::maxFrames;
                if (c < buffer.getNumChannels()) std::memcpy (d, buffer.getReadPointer (c, start), sizeof (float) * (size_t) n);
                else std::memset (d, 0, sizeof (float) * (size_t) n);
            }

            const int32_t seq = ++audSeq;
            s.audSeq = seq;
            SetEvent (conn->audReq);
            // realtime: never stall the audio device for long; offline (export): wait as long as it takes
            const int timeout = offline ? 20000 : juce::jmax (100, (int) (4000.0 * n / juce::jmax (1.0, sampleRate)) + 60);
            if (! conn->waitFor (conn->audRep, [&s, seq] { return s.audDoneSeq == seq; }, timeout))
            {
                stale = true;
                buffer.clear (start, total - start);
                midi.clear();
                return;
            }

            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                if (c < nOut) buffer.copyFrom (c, start, s.audioOut + c * br::maxFrames, n);
                else buffer.clear (c, start, n);
            }
            for (int i = 0; i < juce::jmin ((int) s.numMidiOut, br::maxMidi); ++i)
            {
                const auto& e = s.midiOut[i];
                const int len = juce::MidiMessage::getMessageLengthFromFirstByte (e.bytes[0]);
                midiOut.addEvent (e.bytes, juce::jlimit (1, 3, len), start + juce::jlimit (0, n - 1, (int) e.delta));
            }
            const int np = juce::jmin ((int) s.numParams, (int) values.size());
            for (int p = 0; p < np; ++p)
                if (! dirty[(size_t) p].load()) values[(size_t) p] = s.paramValues[p];
        }
        midi.swapWith (midiOut);
        if (s.latency != lastLatency) { lastLatency = s.latency; setLatencySamples (lastLatency); }
    }

    // ---- programs ----
    int getNumPrograms() override        { return juce::jmax (1, info.numPrograms); }
    int getCurrentProgram() override     { return info.numPrograms > 0 ? (int) dispatchValue (effGetProgram) : 0; }
    void setCurrentProgram (int i) override
    {
        if (info.numPrograms <= 0) return;
        dispatchValue (effBeginSetProgram);
        dispatchValue (effSetProgram, 0, juce::jlimit (0, info.numPrograms - 1, i));
        dispatchValue (effEndSetProgram);
        refreshValues();
    }
    const juce::String getProgramName (int i) override
    {
        if (info.numPrograms <= 0) return {};
        if (conn->control (br::opDispatch, effGetProgramNameIndexed, i, -1, 0.0f, br::ptrStringOut) && conn->shm->ctlResult != 0)
            return conn->dataString().trim();
        if (i == getCurrentProgram()) return dispatchString (effGetProgramName);
        return "Program " + juce::String (i + 1);
    }
    void changeProgramName (int i, const juce::String& name) override
    {
        if (i != getCurrentProgram()) return;
        char buf[32] = {};
        name.copyToUTF8 (buf, 24);
        conn->control (br::opDispatch, effSetProgramName, 0, 0, 0.0f, br::ptrStringIn, buf, (int) std::strlen (buf) + 1);
    }

    // ---- state (same format as directly hosted VST2 plugins) ----
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream out (dest, false);
        if ((info.flags & flagProgramChunks) != 0 && conn->control (br::opDispatch, effGetChunk, 0, 0, 0.0f, br::ptrChunkOut))
        {
            const int size = juce::jlimit (0, br::dataSize, (int) conn->shm->dataLength);
            out.writeInt (0x57564332);   // 'WVC2'
            out.writeInt (size);
            if (size > 0) out.write (conn->shm->data, (size_t) size);
        }
        else
        {
            refreshValues();
            out.writeInt (0x57565032);   // 'WVP2'
            out.writeInt (getCurrentProgram());
            out.writeInt ((int) values.size());
            for (auto& v : values) out.writeFloat (v.load());
        }
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        const int magic = in.readInt();
        if (magic == 0x57564332)
        {
            const int len = in.readInt();
            if (len > 0 && len <= size - 8 && len <= br::dataSize)
                conn->control (br::opDispatch, effSetChunk, 0, len, 0.0f, br::ptrChunkIn, (const char*) data + 8, len);
        }
        else if (magic == 0x57565032)
        {
            const int program = in.readInt();
            const int count = in.readInt();
            if (info.numPrograms > 0) setCurrentProgram (program);
            for (int p = 0; p < count && p < (int) values.size() && ! in.isExhausted(); ++p)
                conn->control (br::opSetParam, 0, p, 0, in.readFloat());
        }
        refreshValues();
    }

    // ---- editor ----
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

private:
    void updateTime (int)
    {
        timeTempo = 120.0; timeSigNum = 4; timeSigDen = 4;
        timeFlags = timeTempoValid | timeTimeSigValid | timePpqPosValid | timeBarsValid;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto smp = pos->getTimeInSamples()) timeSamplePos = (double) *smp;
                if (auto b = pos->getBpm()) timeTempo = *b;
                if (auto ppq = pos->getPpqPosition()) timePpq = *ppq;
                if (auto bar = pos->getPpqPositionOfLastBarStart()) timeBarStart = *bar;
                if (auto ts = pos->getTimeSignature()) { timeSigNum = ts->numerator; timeSigDen = ts->denominator; }
                if (pos->getIsPlaying()) timeFlags |= timeTransportPlaying;
                if (pos->getIsRecording()) timeFlags |= timeTransportRecording;
                if (pos->getIsLooping())
                    if (auto loop = pos->getLoopPoints())
                    {
                        timeFlags |= timeTransportCycleActive | timeCyclePosValid;
                        timeCycleStart = loop->ppqStart;
                        timeCycleEnd = loop->ppqEnd;
                    }
                if (pos->getIsPlaying() != wasPlaying) timeFlags |= timeTransportChanged;
                wasPlaying = pos->getIsPlaying();
            }
    }

    std::unique_ptr<Connection> conn;
    br::Info info;
    juce::PluginDescription desc;
    std::vector<std::atomic<float>> values;
    std::vector<std::atomic<bool>> dirty;
    std::atomic<bool> anyDirty { false };
    double sampleRate = 48000.0;
    bool active = false, stale = false, editorOpen = false, wasPlaying = false;
    int32_t audSeq = 0;
    int lastLatency = 0;
    double timeSamplePos = 0, timeTempo = 120, timePpq = 0, timeBarStart = 0, timeCycleStart = 0, timeCycleEnd = 0;
    int timeSigNum = 4, timeSigDen = 4, timeFlags = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgedInstance)
};

float BridgedParameter::getValue() const    { return owner.cachedValue (index); }
void BridgedParameter::setValue (float v)   { owner.setCachedValue (index, juce::jlimit (0.0f, 1.0f, v)); }
juce::String BridgedParameter::getText (float v, int maxLen) const
{
    if (std::abs (v - getValue()) < 1.0e-5f)
    {
        auto t = owner.dispatchString (effGetParamDisplay, index);
        if (t.isNotEmpty()) return (label.isNotEmpty() ? t + " " + label : t).substring (0, maxLen);
    }
    return juce::String (v, 2);
}

/** The plugin draws its own editor in a window of its own (it lives in the 32-bit bridge process); this panel
    opens it, brings it back and tells you what's going on. */
class BridgedEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BridgedEditor (BridgedInstance& p) : AudioProcessorEditor (p), owner (p)
    {
        title.setText (p.getName(), juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colour (0xffe8eaf0));
        addAndMakeVisible (title);
        text.setFont (juce::Font (juce::FontOptions (13.0f)));
        text.setColour (juce::Label::textColourId, juce::Colour (0xff8a92a6));
        text.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (text);
        show.onClick = [this] { owner.showEditor(); };
        addAndMakeVisible (show);
        if (! owner.pluginHasEditor())
        {
            // no window of its own: its parameters as sliders, right here
            generic = std::make_unique<juce::GenericAudioProcessorEditor> (owner);
            addAndMakeVisible (*generic);
            show.setVisible (false);
        }
        wantOpen = owner.pluginHasEditor();   // opened on the first timer tick, so it appears in front of this window
        update();
        setSize (generic != nullptr ? juce::jmax (440, generic->getWidth()) : 440,
                 generic != nullptr ? 90 + juce::jmin (500, generic->getHeight()) : 150);
        startTimer (250);
    }

    ~BridgedEditor() override
    {
        stopTimer();
        generic.reset();
        if (owner.pluginHasEditor()) owner.closeEditor();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff161920));
        g.setColour (juce::Colour (0xff7c5cff));
        g.fillRect (getLocalBounds().removeFromTop (3));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        title.setBounds (r.removeFromTop (26));
        if (generic != nullptr)
        {
            text.setBounds (r.removeFromTop (40));
            generic->setBounds (r);
            return;
        }
        auto buttons = r.removeFromBottom (30);
        show.setBounds (buttons.removeFromLeft (170));
        text.setBounds (r);
    }

private:
    void timerCallback() override
    {
        if (wantOpen) { wantOpen = false; owner.openEditor(); }
        update();
    }
    void update()
    {
        if (! owner.isAlive())
            text.setText ("The plugin stopped (it may have crashed). Remove it and add it again to restart it.", juce::dontSendNotification);
        else if (! owner.pluginHasEditor())
            text.setText ("This 32-bit plugin has no window of its own: here are its settings.", juce::dontSendNotification);
        else
            text.setText (owner.isEditorShowing() ? "This is a 32-bit plugin: it plays in its own window, which is open now."
                                                  : "This is a 32-bit plugin: its window is closed. 'Show its window' brings it back.", juce::dontSendNotification);
        show.setEnabled (owner.isAlive() && owner.pluginHasEditor());
    }

    BridgedInstance& owner;
    juce::Label title, text;
    juce::TextButton show { "Show its window" };
    std::unique_ptr<juce::GenericAudioProcessorEditor> generic;
    bool wantOpen = false;
};

juce::AudioProcessorEditor* BridgedInstance::createEditor() { return new BridgedEditor (*this); }

// =====================================================================================================
//  Entry points
// =====================================================================================================
bool describe (const juce::File& dll, juce::PluginDescription& out, juce::String& error)
{
    Connection c;
    br::Info info;
    if (! startAndInit (c, dll, info, error)) return false;
    if (info.category == categoryShell) { error = "32-bit shell plugins aren't supported"; return false; }
    fillDescription (info, dll, out);
    return true;
}

std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
    auto c = std::make_unique<Connection>();
    br::Info info;
    if (! startAndInit (*c, juce::File (d.fileOrIdentifier), info, error)) return {};
    if (info.numInputs > br::maxChannels || info.numOutputs > br::maxChannels)
    {
        error = d.name + " has more channels than the bridge supports";
        return {};
    }
    auto inst = std::make_unique<BridgedInstance> (std::move (c), info, d);
    inst->setRateAndBufferSizeDetails (sr, block);
    return inst;
}

#else   // not 64-bit Windows: nothing to bridge

bool describe (const juce::File& dll, juce::PluginDescription&, juce::String& error)
{
    error = dll.getFileName() + " is a 32-bit Windows plugin";
    return false;
}

std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d, double, int, juce::String& error)
{
    error = d.name + " is a 32-bit Windows plugin";
    return {};
}

#endif

} // namespace wis::daw::vst2bridge
