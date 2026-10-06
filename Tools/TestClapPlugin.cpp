// A minimal CLAP instrument used only by the automated tests (dawtest): audio in -> out * gain, a sine for each
// held note, 32 samples of reported latency, saveable state, and a read-only parameter with the host tempo.
#include <clap/clap.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace
{
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr };

const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT, "com.womaninstem.test-synth", "WIS Test Synth CLAP", "WOMANINSTEM Tests", "", "", "", "1.0.0",
    "Test plugin for the automated tests", features
};

constexpr clap_id gainId = 7, tempoId = 8;

struct Plugin
{
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    double gain = 1.0;
    double tempoSeen = 0.0;
    std::map<int, double> notes;
};

Plugin* self (const clap_plugin_t* p) { return static_cast<Plugin*> (p->plugin_data); }

// ---- params ----
uint32_t paramCount (const clap_plugin_t*) { return 2; }
bool paramInfo (const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    std::memset (info, 0, sizeof (*info));
    if (index == 0) { info->id = gainId; info->flags = CLAP_PARAM_IS_AUTOMATABLE; std::strcpy (info->name, "Gain"); info->min_value = 0; info->max_value = 2; info->default_value = 1; return true; }
    if (index == 1) { info->id = tempoId; info->flags = CLAP_PARAM_IS_READONLY; std::strcpy (info->name, "Tempo"); info->min_value = 0; info->max_value = 300; info->default_value = 0; return true; }
    return false;
}
bool paramValue (const clap_plugin_t* p, clap_id id, double* v)
{
    if (id == gainId) { *v = self (p)->gain; return true; }
    if (id == tempoId) { *v = self (p)->tempoSeen; return true; }
    return false;
}
bool valueToText (const clap_plugin_t*, clap_id id, double v, char* out, uint32_t cap)
{
    std::snprintf (out, cap, id == gainId ? "%.2f x" : "%.1f bpm", v);
    return true;
}
bool textToValue (const clap_plugin_t*, clap_id, const char* text, double* v) { *v = std::atof (text); return true; }
void handleEvent (Plugin* s, const clap_event_header_t* h)
{
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (h->type == CLAP_EVENT_PARAM_VALUE)
    {
        auto* e = (const clap_event_param_value_t*) h;
        if (e->param_id == gainId) s->gain = e->value;
    }
    else if (h->type == CLAP_EVENT_NOTE_ON)  s->notes[((const clap_event_note_t*) h)->key] = 0.0;
    else if (h->type == CLAP_EVENT_NOTE_OFF) s->notes.erase (((const clap_event_note_t*) h)->key);
    else if (h->type == CLAP_EVENT_MIDI)
    {
        auto* m = (const clap_event_midi_t*) h;
        const int st = m->data[0] & 0xf0;
        if (st == 0x90 && m->data[2] > 0) s->notes[m->data[1]] = 0.0;
        else if (st == 0x80 || st == 0x90) s->notes.erase (m->data[1]);
    }
}
void paramFlush (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
{
    for (uint32_t i = 0; i < in->size (in); ++i) handleEvent (self (p), in->get (in, i));
}
const clap_plugin_params_t params = { paramCount, paramInfo, paramValue, valueToText, textToValue, paramFlush };

// ---- ports ----
uint32_t audioPortCount (const clap_plugin_t*, bool) { return 1; }
bool audioPortGet (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0) return false;
    std::memset (info, 0, sizeof (*info));
    info->id = isInput ? 0 : 1;
    std::strcpy (info->name, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts = { audioPortCount, audioPortGet };

uint32_t notePortCount (const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
bool notePortGet (const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (index != 0 || ! isInput) return false;
    std::memset (info, 0, sizeof (*info));
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strcpy (info->name, "Notes");
    return true;
}
const clap_plugin_note_ports_t notePorts = { notePortCount, notePortGet };

uint32_t latency (const clap_plugin_t*) { return 32; }
const clap_plugin_latency_t latencyExt = { latency };

bool saveState (const clap_plugin_t* p, const clap_ostream_t* s) { return s->write (s, &self (p)->gain, sizeof (double)) == sizeof (double); }
bool loadState (const clap_plugin_t* p, const clap_istream_t* s) { return s->read (s, &self (p)->gain, sizeof (double)) == sizeof (double); }
const clap_plugin_state_t state = { saveState, loadState };

// ---- plugin ----
bool init (const clap_plugin_t*) { return true; }
void destroy (const clap_plugin_t* p) { delete self (p); }
bool activate (const clap_plugin_t* p, double sr, uint32_t, uint32_t) { self (p)->sampleRate = sr; return true; }
void deactivate (const clap_plugin_t*) {}
bool startProcessing (const clap_plugin_t*) { return true; }
void stopProcessing (const clap_plugin_t*) {}
void reset (const clap_plugin_t* p) { self (p)->notes.clear(); }

clap_process_status process (const clap_plugin_t* p, const clap_process_t* proc)
{
    auto* s = self (p);
    if (proc->transport != nullptr && (proc->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) s->tempoSeen = proc->transport->tempo;
    const uint32_t n = proc->frames_count;
    const uint32_t numEvents = proc->in_events->size (proc->in_events);
    uint32_t next = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        while (next < numEvents)
        {
            auto* h = proc->in_events->get (proc->in_events, next);
            if (h->time > i) break;
            handleEvent (s, h);
            ++next;
        }
        float sine = 0.0f;
        for (auto& [key, phase] : s->notes)
        {
            sine += 0.2f * (float) std::sin (phase);
            phase += 2.0 * 3.14159265358979 * 440.0 * std::pow (2.0, (key - 69) / 12.0) / s->sampleRate;
        }
        for (uint32_t c = 0; c < 2; ++c)
            proc->audio_outputs[0].data32[c][i] = proc->audio_inputs[0].data32[c][i] * (float) s->gain + sine;
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* getExtension (const clap_plugin_t*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &state;
    return nullptr;
}
void onMainThread (const clap_plugin_t*) {}

// ---- factory / entry ----
uint32_t count (const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* describe (const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &descriptor : nullptr; }
const clap_plugin_t* create (const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (std::strcmp (id, descriptor.id) != 0) return nullptr;
    auto* s = new Plugin();
    s->host = host;
    s->plugin = { &descriptor, s, init, destroy, activate, deactivate, startProcessing, stopProcessing, reset, process, getExtension, onMainThread };
    return &s->plugin;
}
const clap_plugin_factory_t factory = { count, describe, create };

bool entryInit (const char*) { return true; }
void entryDeinit() {}
const void* getFactory (const char* id) { return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, entryInit, entryDeinit, getFactory };
