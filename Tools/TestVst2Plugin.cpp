// A minimal VST2 instrument used only by the automated tests (dawtest): audio in -> out * gain, plus a sine
// for each held MIDI note. Parameter 1 reports the tempo it last saw from the host.
#include "../Source/Daw/Plugins/Vst2Abi.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <vector>

using namespace wis::vst2;

#if defined (_WIN32)
 #define WIS_EXPORT extern "C" __declspec(dllexport)
#else
 #define WIS_EXPORT extern "C" __attribute__ ((visibility ("default")))
#endif

namespace
{
struct TestPlugin
{
    Effect effect {};
    HostCallback host = nullptr;
    float gain = 0.5f;
    float tempoSeen = 0.0f;
    double sampleRate = 48000.0;
    std::map<int, double> notes;   // key -> phase
    struct Pending { int frame, status, key, vel; };
    std::vector<Pending> pending;  // events for the next block, applied sample-accurately
    int program = 0;
    char chunk[4] {};
};

TestPlugin* self (Effect* e) { return static_cast<TestPlugin*> (e->object); }

intptr_t WIS_VST_CALL dispatcher (Effect* e, int32_t op, int32_t index, intptr_t value, void* ptr, float opt)
{
    auto* p = self (e);
    switch (op)
    {
        case effClose:          delete p; return 1;
        case effSetSampleRate:  p->sampleRate = opt; return 1;
        case effGetParamName:   std::strcpy ((char*) ptr, index == 0 ? "Gain" : "Tempo"); return 1;
        case effGetParamLabel:  std::strcpy ((char*) ptr, index == 0 ? "x" : "bpm"); return 1;
        case effGetParamDisplay:
        {
            const float v = index == 0 ? p->gain * 2.0f : p->tempoSeen * 300.0f;
            std::snprintf ((char*) ptr, 16, "%.2f", v);
            return 1;
        }
        case effGetEffectName:  std::strcpy ((char*) ptr, "WIS Test Synth VST2"); return 1;
        case effGetVendorString: std::strcpy ((char*) ptr, "WOMANINSTEM Tests"); return 1;
        case effGetProductString: std::strcpy ((char*) ptr, "WIS Test Synth VST2"); return 1;
        case effGetVendorVersion: return 1000;
        case effGetPlugCategory: return categorySynth;
        case effGetVstVersion:  return 2400;
        case effGetProgram:     return p->program;
        case effSetProgram:     p->program = (int) value; return 1;
        case effGetProgramNameIndexed: std::strcpy ((char*) ptr, index == 0 ? "Init" : "Loud"); return 1;
        case effGetChunk:
            std::memcpy (p->chunk, &p->gain, 4);
            *(void**) ptr = p->chunk;
            return 4;
        case effSetChunk:
            if (value == 4) std::memcpy (&p->gain, ptr, 4);
            return 1;
        case effProcessEvents:
        {
            auto* ev = (Events*) ptr;
            for (int i = 0; i < ev->numEvents; ++i)
                if (ev->events[i]->type == eventTypeMidi)
                {
                    auto* me = (MidiEvent*) ev->events[i];
                    p->pending.push_back ({ me->deltaFrames, (unsigned char) me->midiData[0] & 0xf0, me->midiData[1], me->midiData[2] });
                }
            return 1;
        }
        case effCanDo:
        {
            const char* what = (const char*) ptr;
            return (std::strcmp (what, "receiveVstEvents") == 0 || std::strcmp (what, "receiveVstMidiEvent") == 0) ? 1 : 0;
        }
        default: return 0;
    }
}

void WIS_VST_CALL setParameter (Effect* e, int32_t i, float v) { if (i == 0) self (e)->gain = v; }
float WIS_VST_CALL getParameter (Effect* e, int32_t i) { return i == 0 ? self (e)->gain : self (e)->tempoSeen; }

void WIS_VST_CALL processReplacing (Effect* e, float** in, float** out, int32_t n)
{
    auto* p = self (e);
    if (auto* t = (TimeInfo*) p->host (e, hostGetTime, 0, timeTempoValid, nullptr, 0.0f))
        if (t->flags & timeTempoValid) p->tempoSeen = (float) (t->tempo / 300.0);
    size_t next = 0;
    for (int i = 0; i < n; ++i)
    {
        while (next < p->pending.size() && p->pending[next].frame <= i)
        {
            auto& ev = p->pending[next++];
            if (ev.status == 0x90 && ev.vel > 0) p->notes[ev.key] = 0.0;
            else if (ev.status == 0x80 || ev.status == 0x90) p->notes.erase (ev.key);
        }
        float s = 0.0f;
        for (auto& [key, phase] : p->notes)
        {
            s += 0.2f * (float) std::sin (phase);
            phase += 2.0 * 3.14159265358979 * 440.0 * std::pow (2.0, (key - 69) / 12.0) / p->sampleRate;
        }
        out[0][i] = in[0][i] * p->gain * 2.0f + s;
        out[1][i] = in[1][i] * p->gain * 2.0f + s;
    }
    p->pending.clear();
}
}

WIS_EXPORT Effect* VSTPluginMain (HostCallback host)
{
    if (host (nullptr, hostVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;
    auto* p = new TestPlugin();
    p->host = host;
    auto& e = p->effect;
    e.magic = effectMagic;
    e.dispatcher = dispatcher;
    e.setParameter = setParameter;
    e.getParameter = getParameter;
    e.processReplacing = processReplacing;
    e.numPrograms = 2;
    e.numParams = 2;
    e.numInputs = 2;
    e.numOutputs = 2;
    e.flags = flagCanReplacing | flagIsSynth | flagProgramChunks;
    e.initialDelay = 0;
    e.uniqueId = 0x57495354;   // 'WIST'
    e.version = 1000;
    e.object = p;
    return &e;
}
