#pragma once

/*  The 32-bit plugin bridge: WOMANINSTEM (64-bit) runs an old 32-bit VST2 plugin inside wisbridge32.exe and
    talks to it through one block of named shared memory plus four named events:

        <name>.ctlReq / <name>.ctlRep   control calls (open, programs, state, editor...), handled on the
                                        bridge's GUI thread, one at a time
        <name>.audReq / <name>.audRep   one audio block, handled on the bridge's audio thread

    Every request carries a sequence number that the reply echoes, so a late reply is never mistaken for the
    current one. Only fixed-size types are used: the layout is identical in the 64-bit host and the 32-bit bridge
    (checked by the static_asserts below, compiled into both). No JUCE in here: the bridge is plain Win32. */

#include <cstdint>
#include <cstddef>

namespace wis::bridge
{

constexpr int32_t protocolVersion = 1;
constexpr int maxChannels  = 8;
constexpr int maxFrames    = 2048;      // per audio call; the host splits bigger blocks
constexpr int maxMidi      = 512;
constexpr int maxParamSets = 256;
constexpr int maxParams    = 1024;      // values reported back after every block
constexpr int dataSize     = 4 << 20;   // control payload (state chunks, strings): 4 MB

enum Op : int32_t
{
    opNone = 0,
    opInit,             // -> result 1 + Info in data, or 0 + error text in data
    opDispatch,         // plain dispatcher call; `kind` says what `ptr` is
    opGetParam,         // index -> result = value bits (float in ctlOpt)
    opSetParam,         // index, ctlOpt
    opGetAllParams,     // -> data = float[numParams]
    opPrepare,          // ctlOpt = sample rate, ctlIndex = max block size
    opSuspend,
    opOpenEditor,       // -> result 1 if a window is showing
    opCloseEditor,
    opShowEditor,       // bring the window to the front
    opQuit
};

/** What the dispatcher's `ptr` points at for opDispatch. */
enum PtrKind : int32_t
{
    ptrNone = 0,
    ptrStringOut,       // a 512-byte buffer the plugin fills: returned in data
    ptrStringIn,        // a zero-terminated string from the host, in data
    ptrChunkOut,        // effGetChunk: the chunk is copied into data, dataLength = its size
    ptrChunkIn          // effSetChunk: data holds dataLength bytes (passed as `value` too)
};

struct Info
{
    int32_t numPrograms, numParams, numInputs, numOutputs, flags, uniqueId, version, initialDelay;
    int32_t category, vendorVersion, hasEditor, isSynth;
    char name[128], vendor[128], product[128];
};

struct MidiMsg { int32_t delta; uint8_t bytes[4]; };
struct ParamSet { int32_t index; float value; };

struct Shared
{
    // ---- header ----
    int32_t version;
    int32_t bridgePid;
    int32_t editorOpen;             // bridge: its editor window is showing
    int32_t latency;                // bridge: the plugin's current initialDelay

    // ---- control channel ----
    int32_t ctlSeq, ctlDoneSeq;
    int32_t ctlOp, ctlOpcode, ctlIndex, ctlKind;
    int64_t ctlValue;
    int64_t ctlResult;
    float ctlOpt;
    int32_t dataLength;

    // ---- audio channel ----
    alignas (8) double samplePos;
    double sampleRate, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNum, timeSigDen, timeFlags, nonRealtime;
    int32_t audSeq, audDoneSeq;
    int32_t numFrames, numIn, numOut;
    int32_t numMidiIn, numParamSets, numMidiOut, numParams;
    MidiMsg midiIn[maxMidi];
    ParamSet paramSets[maxParamSets];
    MidiMsg midiOut[maxMidi];
    float paramValues[maxParams];
    float audioIn[maxChannels * maxFrames];
    float audioOut[maxChannels * maxFrames];

    // ---- control payload ----
    char data[dataSize];
};

static_assert (sizeof (MidiMsg) == 8, "bridge layout");
static_assert (sizeof (Info) == 12 * 4 + 3 * 128, "bridge layout");
static_assert (offsetof (Shared, ctlValue) == 40, "bridge layout");
static_assert (offsetof (Shared, samplePos) == 64, "bridge layout");
static_assert (offsetof (Shared, midiIn) % 4 == 0, "bridge layout");
static_assert (sizeof (Shared) % 8 == 0, "bridge layout");

/** Names of the shared objects for one bridged plugin (the base name is passed on the bridge's command line). */
inline void objectName (char* out, size_t outSize, const char* base, const char* suffix)
{
    size_t i = 0;
    for (const char* s = base; *s != 0 && i + 1 < outSize; ++s) out[i++] = *s;
    for (const char* s = suffix; *s != 0 && i + 1 < outSize; ++s) out[i++] = *s;
    out[i] = 0;
}

} // namespace wis::bridge
