// A JUCE-free 64-bit test of the bridge protocol: starts wisbridge32.exe on a 32-bit VST2 plugin, opens it,
// plays a MIDI note through it and checks the sound, the parameters and the state chunk.
//   BridgeSmokeTest.exe <wisbridge32.exe> <plugin.dll>
// (The real host side, Source/Daw/Plugins/Vst2Bridge.cpp, is tested by dawtest on Windows.)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include "../../Source/Daw/Plugins/Vst2Abi.h"
#include "../../Source/Daw/Plugins/BridgeProtocol.h"

namespace br = wis::bridge;
using namespace wis::vst2;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (! ok) ++failures; }

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: BridgeSmokeTest <wisbridge32.exe> <plugin.dll>\n"); return 2; }
    const std::string base = "Local\\WisBridgeSmoke-" + std::to_string (GetCurrentProcessId());
    HANDLE mapping = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof (br::Shared), base.c_str());
    auto* shm = (br::Shared*) MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (br::Shared));
    std::memset (shm, 0, 4096);
    shm->version = br::protocolVersion;
    auto ev = [&] (const char* sfx) { char n[300]; br::objectName (n, sizeof (n), base.c_str(), sfx); return CreateEventA (nullptr, FALSE, FALSE, n); };
    HANDLE ctlReq = ev (".ctlReq"), ctlRep = ev (".ctlRep"), audReq = ev (".audReq"), audRep = ev (".audRep");

    std::string cmd = std::string ("\"") + argv[1] + "\" --host " + std::to_string (GetCurrentProcessId()) + " --shm \"" + base + "\" --dll \"" + argv[2] + "\"";
    STARTUPINFOA si {}; si.cb = sizeof (si);
    PROCESS_INFORMATION pi {};
    if (! CreateProcessA (nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { std::printf ("couldn't start the bridge\n"); return 1; }

    int seq = 0;
    auto control = [&] (int op, int opcode = 0, int index = 0, long long value = 0, float opt = 0.0f, int kind = 0) -> bool
    {
        shm->ctlOp = op; shm->ctlOpcode = opcode; shm->ctlIndex = index; shm->ctlValue = value; shm->ctlOpt = opt; shm->ctlKind = kind;
        shm->ctlSeq = ++seq;
        SetEvent (ctlReq);
        HANDLE w[] = { ctlRep, pi.hProcess };
        while (shm->ctlDoneSeq != seq)
            if (WaitForMultipleObjects (2, w, FALSE, 20000) != WAIT_OBJECT_0) return false;
        return true;
    };

    check (control (br::opInit) && shm->ctlResult == 1, "bridge loads the 32-bit plugin");
    br::Info info;
    std::memcpy (&info, shm->data, sizeof (info));
    std::printf ("  plugin: '%s' by '%s', %d in / %d out, %d params, synth %d\n", info.name, info.vendor, info.numInputs, info.numOutputs, info.numParams, info.isSynth);
    check (std::strncmp (info.name, "WIS Test Synth", 14) == 0 && info.isSynth == 1, "describes it");

    check (control (br::opDispatch, effGetParamName, 0, 0, 0.0f, br::ptrStringOut) && std::strcmp (shm->data, "Gain") == 0, "string calls (parameter name)");
    check (control (br::opPrepare, 0, 512, 0, 48000.0f) && shm->ctlResult == 1, "prepares");
    check (control (br::opSetParam, 0, 0, 0, 0.25f), "sets a parameter");

    // one second with A4 held: count zero crossings
    int aseq = 0, crossings = 0;
    float prev = 0.0f, peak = 0.0f;
    for (int b = 0; b < 48000 / 480; ++b)
    {
        shm->numFrames = 480; shm->numIn = 2; shm->numOut = 2;
        shm->tempo = 133.0; shm->sampleRate = 48000.0; shm->timeFlags = timeTempoValid;
        shm->numMidiIn = 0;
        if (b == 0) { shm->midiIn[0].delta = 0; shm->midiIn[0].bytes[0] = 0x90; shm->midiIn[0].bytes[1] = 69; shm->midiIn[0].bytes[2] = 100; shm->numMidiIn = 1; }
        shm->numParamSets = 0;
        std::memset (shm->audioIn, 0, sizeof (float) * 2 * br::maxFrames);
        shm->audSeq = ++aseq;
        SetEvent (audReq);
        HANDLE w[] = { audRep, pi.hProcess };
        while (shm->audDoneSeq != aseq)
            if (WaitForMultipleObjects (2, w, FALSE, 5000) != WAIT_OBJECT_0) { check (false, "audio block answered"); return 1; }
        for (int i = 0; i < 480; ++i)
        {
            const float x = shm->audioOut[i];
            peak = std::max (peak, std::fabs (x));
            if (prev < 0.0f && x >= 0.0f) ++crossings;
            prev = x;
        }
    }
    std::printf ("  %d rising zero crossings in 1 s, peak %.3f\n", crossings, peak);
    check (std::abs (crossings - 440) <= 2 && peak > 0.1f, "plays A4 from MIDI");
    check (shm->numParams == 2 && std::fabs (shm->paramValues[1] * 300.0f - 133.0f) < 0.5f, "the plugin sees the host's tempo, its parameters come back");
    check (std::fabs (shm->paramValues[0] - 0.25f) < 1e-6f, "the parameter set earlier stuck");

    check (control (br::opDispatch, effGetChunk, 0, 0, 0.0f, br::ptrChunkOut) && shm->dataLength == 4, "state chunk comes back");
    float g = 0; std::memcpy (&g, shm->data, 4);
    check (std::fabs (g - 0.25f) < 1e-6f, "...with the right contents");

    check (control (br::opOpenEditor) && shm->ctlResult == 1 && shm->editorOpen == 1, "opens the plugin's own window");
    HWND win = FindWindowW (L"WisBridgeEditor", nullptr);
    RECT rc {};
    if (win != nullptr) GetClientRect (win, &rc);
    std::printf ("  editor window %p, client %ldx%ld\n", (void*) win, rc.right - rc.left, rc.bottom - rc.top);
    check (win != nullptr && rc.right - rc.left == 240 && rc.bottom - rc.top == 120, "...sized to the plugin's editor");
    Sleep (300);
    check (control (br::opCloseEditor) && shm->editorOpen == 0 && FindWindowW (L"WisBridgeEditor", nullptr) == nullptr, "closes it");
    check (control (br::opQuit), "quits");
    check (WaitForSingleObject (pi.hProcess, 5000) == WAIT_OBJECT_0, "bridge process exits");
    std::printf (failures == 0 ? "BRIDGE SMOKE TEST PASSED\n" : "BRIDGE SMOKE TEST FAILED\n");
    return failures == 0 ? 0 : 1;
}
