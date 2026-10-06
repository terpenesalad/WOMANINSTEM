#pragma once

/*  The VST 2.4 binary interface, declared from its publicly documented layout so WOMANINSTEM can host
    VST2 plugins without the (no longer distributed) Steinberg SDK. Only what a host needs is declared.
    Names are our own; numeric values and struct layouts are what the ABI requires.
    VST is a trademark of Steinberg Media Technologies GmbH. */

#include <cstdint>
#include <cstddef>

namespace wis::vst2
{

#if defined (_WIN32) && ! defined (_WIN64)
 #define WIS_VST_CALL __cdecl
#else
 #define WIS_VST_CALL
#endif

struct Effect;

using HostCallback        = intptr_t (WIS_VST_CALL*) (Effect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
using DispatcherProc      = intptr_t (WIS_VST_CALL*) (Effect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
using ProcessProc         = void     (WIS_VST_CALL*) (Effect*, float** inputs, float** outputs, int32_t frames);
using ProcessDoubleProc   = void     (WIS_VST_CALL*) (Effect*, double** inputs, double** outputs, int32_t frames);
using SetParameterProc    = void     (WIS_VST_CALL*) (Effect*, int32_t index, float value);
using GetParameterProc    = float    (WIS_VST_CALL*) (Effect*, int32_t index);
using EntryProc           = Effect*  (WIS_VST_CALL*) (HostCallback);

constexpr int32_t effectMagic = 0x56737450;   // 'VstP'

/** The plugin instance as seen through the ABI ("AEffect"). */
struct Effect
{
    int32_t magic;
    DispatcherProc dispatcher;
    ProcessProc processAccumulating;     // deprecated
    SetParameterProc setParameter;
    GetParameterProc getParameter;
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    intptr_t hostReserved1;              // free for the host: we keep a pointer to our wrapper here
    intptr_t hostReserved2;
    int32_t initialDelay;
    int32_t realQualities;               // deprecated
    int32_t offQualities;                // deprecated
    float ioRatio;                       // deprecated
    void* object;
    void* user;
    int32_t uniqueId;
    int32_t version;
    ProcessProc processReplacing;
    ProcessDoubleProc processDoubleReplacing;
    char future[56];
};

// Effect::flags
enum : int32_t
{
    flagHasEditor          = 1 << 0,
    flagCanReplacing       = 1 << 4,
    flagProgramChunks      = 1 << 5,
    flagIsSynth            = 1 << 8,
    flagNoSoundInStop      = 1 << 9,
    flagCanDoubleReplacing = 1 << 12
};

// dispatcher opcodes (host -> plugin)
enum : int32_t
{
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4, effGetProgramName = 5,
    effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
    effEditGetRect = 13, effEditOpen = 14, effEditClose = 15, effEditIdle = 19,
    effGetChunk = 23, effSetChunk = 24,
    effProcessEvents = 25, effCanBeAutomated = 26, effString2Parameter = 27, effGetProgramNameIndexed = 29,
    effGetInputProperties = 33, effGetOutputProperties = 34, effGetPlugCategory = 35,
    effSetSpeakerArrangement = 42, effSetBypass = 44, effGetEffectName = 45,
    effGetVendorString = 47, effGetProductString = 48, effGetVendorVersion = 49, effVendorSpecific = 50,
    effCanDo = 51, effGetTailSize = 52, effGetParameterProperties = 56, effGetVstVersion = 58,
    effEditKeyDown = 59, effEditKeyUp = 60,
    effBeginSetProgram = 67, effEndSetProgram = 68, effShellGetNextPlugin = 70,
    effStartProcess = 71, effStopProcess = 72, effSetProcessPrecision = 77
};

// host callback opcodes (plugin -> host)
enum : int32_t
{
    hostAutomate = 0, hostVersion = 1, hostCurrentId = 2, hostIdle = 3,
    hostGetTime = 7, hostProcessEvents = 8, hostIOChanged = 13, hostSizeWindow = 15,
    hostGetSampleRate = 16, hostGetBlockSize = 17, hostGetInputLatency = 18, hostGetOutputLatency = 19,
    hostGetCurrentProcessLevel = 23, hostGetAutomationState = 24,
    hostGetVendorString = 32, hostGetProductString = 33, hostGetVendorVersion = 34, hostVendorSpecific = 35,
    hostCanDo = 37, hostGetLanguage = 38, hostGetDirectory = 41, hostUpdateDisplay = 42,
    hostBeginEdit = 43, hostEndEdit = 44, hostOpenFileSelector = 45, hostCloseFileSelector = 46
};

// effGetPlugCategory results
enum : int32_t
{
    categoryUnknown = 0, categoryEffect = 1, categorySynth = 2, categoryAnalysis = 3, categoryMastering = 4,
    categorySpatial = 5, categoryRoomFx = 6, categorySurroundFx = 7, categoryRestoration = 8,
    categoryOffline = 9, categoryShell = 10, categoryGenerator = 11
};

enum : int32_t { processLevelUser = 1, processLevelRealtime = 2, processLevelOffline = 4 };

constexpr int32_t eventTypeMidi = 1;
constexpr int32_t eventTypeSysex = 6;

struct Event
{
    int32_t type;
    int32_t byteSize;
    int32_t deltaFrames;
    int32_t flags;
    char data[16];
};

struct MidiEvent
{
    int32_t type;            // eventTypeMidi
    int32_t byteSize;        // sizeof (MidiEvent)
    int32_t deltaFrames;
    int32_t flags;           // 1 = realtime
    int32_t noteLength;
    int32_t noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};

struct SysexEvent
{
    int32_t type;            // eventTypeSysex
    int32_t byteSize;
    int32_t deltaFrames;
    int32_t flags;
    int32_t dumpBytes;
    intptr_t reserved1;
    char* sysexDump;
    intptr_t reserved2;
};

/** Variable-length list: allocate with room for `numEvents` pointers. */
struct Events
{
    int32_t numEvents;
    intptr_t reserved;
    Event* events[2];
};

struct TimeInfo
{
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int32_t smpteOffset;
    int32_t smpteFrameRate;
    int32_t samplesToNextClock;
    int32_t flags;
};

// TimeInfo::flags
enum : int32_t
{
    timeTransportChanged = 1, timeTransportPlaying = 2, timeTransportCycleActive = 4, timeTransportRecording = 8,
    timeNanosValid = 1 << 8, timePpqPosValid = 1 << 9, timeTempoValid = 1 << 10, timeBarsValid = 1 << 11,
    timeCyclePosValid = 1 << 12, timeTimeSigValid = 1 << 13
};

struct Rect { int16_t top, left, bottom, right; };

static_assert (sizeof (MidiEvent) == 32, "VST2 MIDI event layout");
static_assert (sizeof (TimeInfo) == 8 * 8 + 6 * 4, "VST2 time info layout");
static_assert (offsetof (Effect, numPrograms) == 5 * sizeof (void*), "VST2 effect layout");
static_assert (offsetof (Effect, processReplacing) == (sizeof (void*) == 8 ? 120 : 80), "VST2 effect layout");

} // namespace wis::vst2
