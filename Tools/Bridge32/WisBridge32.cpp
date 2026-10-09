// wisbridge32.exe: runs one 32-bit VST2 plugin for the 64-bit WOMANINSTEM (see Source/Daw/Plugins/BridgeProtocol.h).
//
//   wisbridge32.exe --host <pid> --shm <name> --dll <path to plugin.dll>
//
// The GUI thread answers control calls (and runs the plugin's editor window); a second thread renders audio.
// If the plugin crashes, only this process goes down: the app keeps running and reports it.
// Plain Win32 + C++17, built 32-bit:
//   cl /O2 /EHsc /MT /std:c++17 WisBridge32.cpp /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:WINDOWS

#ifndef UNICODE
 #define UNICODE
#endif
#ifndef _UNICODE
 #define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include "../../Source/Daw/Plugins/Vst2Abi.h"
#include "../../Source/Daw/Plugins/BridgeProtocol.h"

using namespace wis::vst2;
namespace br = wis::bridge;

namespace
{
    br::Shared* shm = nullptr;
    HANDLE ctlReq = nullptr, ctlRep = nullptr, audReq = nullptr, audRep = nullptr, hostProcess = nullptr, quitEvent = nullptr;
    HMODULE module = nullptr;
    Effect* effect = nullptr;
    std::string loadError;
    int32_t currentId = 0;
    double sampleRate = 48000.0;
    int32_t blockSize = 512;
    bool processing = false;

    // audio thread state
    TimeInfo timeInfo {};
    thread_local bool onAudioThread = false;
    std::vector<MidiEvent> midiEvents (br::maxMidi);
    std::vector<char> eventBlock (sizeof (Events) + sizeof (Event*) * br::maxMidi);
    int midiOutCount = 0;

    // editor
    HWND window = nullptr;
    bool editorOpen = false;
    const UINT_PTR idleTimer = 1;
    std::wstring windowTitle = L"Plugin";

    void setWindowSizeForEditor (int w, int h)
    {
        if (window == nullptr || w <= 0 || h <= 0) return;
        RECT r { 0, 0, w, h };
        const DWORD style = (DWORD) GetWindowLongW (window, GWL_STYLE);
        AdjustWindowRect (&r, style, FALSE);
        SetWindowPos (window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    intptr_t WIS_VST_CALL hostCallback (Effect* e, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
    {
        (void) opt;
        switch (opcode)
        {
            case hostVersion:          return 2400;
            case hostCurrentId:        return currentId != 0 ? currentId : (e != nullptr ? e->uniqueId : 0);
            case hostIdle:             return 0;
            case 6:                    return 1;   // audioMasterWantMidi (VST 2.0)
            case hostGetTime:          return (intptr_t) &timeInfo;
            case hostProcessEvents:
                if (onAudioThread && ptr != nullptr && shm != nullptr)
                {
                    auto* ev = static_cast<Events*> (ptr);
                    for (int i = 0; i < ev->numEvents && midiOutCount < br::maxMidi; ++i)
                        if (ev->events[i] != nullptr && ev->events[i]->type == eventTypeMidi)
                        {
                            auto* me = reinterpret_cast<MidiEvent*> (ev->events[i]);
                            auto& out = shm->midiOut[midiOutCount++];
                            out.delta = me->deltaFrames;
                            std::memcpy (out.bytes, me->midiData, 4);
                        }
                }
                return 1;
            case hostIOChanged:        if (shm != nullptr && e != nullptr) shm->latency = e->initialDelay; return 1;
            case hostSizeWindow:       setWindowSizeForEditor (index, (int) value); return 1;
            case hostGetSampleRate:    return (intptr_t) sampleRate;
            case hostGetBlockSize:     return blockSize;
            case hostGetInputLatency:  return 0;
            case hostGetOutputLatency: return 0;
            case hostGetCurrentProcessLevel: return onAudioThread ? processLevelRealtime : processLevelUser;
            case hostGetAutomationState:     return 1;
            case hostGetVendorString:  if (ptr) std::strcpy (static_cast<char*> (ptr), "WOMANINSTEM"); return 1;
            case hostGetProductString: if (ptr) std::strcpy (static_cast<char*> (ptr), "WOMANINSTEM Studio"); return 1;
            case hostGetVendorVersion: return 3000;
            case hostGetLanguage:      return 1;
            case hostUpdateDisplay:    return 1;
            case hostCanDo:
            {
                if (ptr == nullptr) return 0;
                const char* what = static_cast<const char*> (ptr);
                for (const char* yes : { "sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "receiveVstEvents", "receiveVstMidiEvent",
                                         "sizeWindow", "startStopProcess" })
                    if (std::strcmp (what, yes) == 0) return 1;
                return 0;
            }
            default: return 0;
        }
    }

    intptr_t dispatch (int32_t op, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0f)
    {
        return effect != nullptr ? effect->dispatcher (effect, op, index, value, ptr, opt) : 0;
    }

    std::string getString (int32_t op, int32_t index = 0)
    {
        char buf[512] = {};
        dispatch (op, index, 0, buf, 0.0f);
        buf[sizeof (buf) - 1] = 0;
        return buf;
    }

    void copyString (char* dest, size_t size, const std::string& s)
    {
        std::strncpy (dest, s.c_str(), size - 1);
        dest[size - 1] = 0;
    }

    bool loadPlugin (const std::wstring& path)
    {
        module = LoadLibraryW (path.c_str());
        if (module == nullptr)
        {
            loadError = "Windows couldn't load the plugin file (error " + std::to_string (GetLastError()) + ")";
            return false;
        }
        auto entry = (EntryProc) (void*) GetProcAddress (module, "VSTPluginMain");
        if (entry == nullptr) entry = (EntryProc) (void*) GetProcAddress (module, "main");
        if (entry == nullptr) { loadError = "This file isn't a VST2 plugin"; return false; }
        effect = entry (hostCallback);
        if (effect == nullptr || effect->magic != effectMagic) { effect = nullptr; loadError = "The plugin didn't start"; return false; }
        dispatch (effOpen);
        shm->latency = effect->initialDelay;
        return true;
    }

    // ---- editor window ------------------------------------------------------------------------------
    LRESULT CALLBACK windowProc (HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
            case WM_TIMER:
                if (wp == idleTimer && editorOpen) dispatch (effEditIdle);
                return 0;
            case WM_CLOSE:
                // closing the plugin's window just hides it; the app can show it again
                ShowWindow (h, SW_HIDE);
                if (shm != nullptr) shm->editorOpen = 0;
                return 0;
            default:
                return DefWindowProcW (h, msg, wp, lp);
        }
    }

    bool openEditor()
    {
        if (effect == nullptr || (effect->flags & flagHasEditor) == 0) return false;
        if (window == nullptr)
        {
            WNDCLASSW wc {};
            wc.lpfnWndProc = windowProc;
            wc.hInstance = GetModuleHandleW (nullptr);
            wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
            wc.lpszClassName = L"WisBridgeEditor";
            wc.hIcon = LoadIcon (nullptr, IDI_APPLICATION);
            RegisterClassW (&wc);
            window = CreateWindowExW (0, wc.lpszClassName, windowTitle.c_str(),
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                      CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
            if (window == nullptr) return false;
        }
        if (! editorOpen)
        {
            // like most hosts: ask for the size, open, then ask again (some editors only know it once open)
            auto applyRect = []
            {
                Rect* r = nullptr;
                dispatch (effEditGetRect, 0, 0, &r);
                if (r != nullptr && r->right > r->left && r->bottom > r->top)
                    setWindowSizeForEditor (r->right - r->left, r->bottom - r->top);
            };
            applyRect();
            dispatch (effEditOpen, 0, 0, window);
            applyRect();
            editorOpen = true;
            SetTimer (window, idleTimer, 33, nullptr);
        }
        ShowWindow (window, SW_SHOWNORMAL);
        SetForegroundWindow (window);
        shm->editorOpen = 1;
        return true;
    }

    void closeEditor()
    {
        if (window == nullptr) return;
        if (editorOpen)
        {
            KillTimer (window, idleTimer);
            dispatch (effEditClose);
            editorOpen = false;
        }
        DestroyWindow (window);
        window = nullptr;
        shm->editorOpen = 0;
    }

    // ---- control ------------------------------------------------------------------------------------
    void fillInfo (br::Info& info)
    {
        std::memset (&info, 0, sizeof (info));
        info.numPrograms = effect->numPrograms;
        info.numParams = effect->numParams;
        info.numInputs = effect->numInputs;
        info.numOutputs = effect->numOutputs;
        info.flags = effect->flags;
        info.uniqueId = effect->uniqueId;
        info.version = effect->version;
        info.initialDelay = effect->initialDelay;
        info.category = (int32_t) dispatch (effGetPlugCategory);
        info.vendorVersion = (int32_t) dispatch (effGetVendorVersion);
        info.hasEditor = (effect->flags & flagHasEditor) != 0 ? 1 : 0;
        info.isSynth = ((effect->flags & flagIsSynth) != 0 || info.category == categorySynth) ? 1 : 0;
        copyString (info.name, sizeof (info.name), getString (effGetEffectName));
        copyString (info.vendor, sizeof (info.vendor), getString (effGetVendorString));
        copyString (info.product, sizeof (info.product), getString (effGetProductString));
    }

    bool handleControl()
    {
        auto& s = *shm;
        bool quit = false;
        s.ctlResult = 0;
        switch (s.ctlOp)
        {
            case br::opInit:
                if (effect == nullptr)
                {
                    copyString (s.data, 512, loadError.empty() ? std::string ("The plugin didn't load") : loadError);
                    s.ctlResult = 0;
                }
                else
                {
                    br::Info info;
                    fillInfo (info);
                    std::memcpy (s.data, &info, sizeof (info));
                    s.dataLength = (int32_t) sizeof (info);
                    s.ctlResult = 1;
                    if (info.name[0] != 0)
                    {
                        const std::string n (info.name);
                        windowTitle = std::wstring (n.begin(), n.end()) + L"  (32-bit)";
                    }
                }
                break;

            case br::opDispatch:
            {
                if (effect == nullptr) break;
                switch (s.ctlKind)
                {
                    case br::ptrStringOut:
                    {
                        std::memset (s.data, 0, 1024);
                        s.ctlResult = dispatch (s.ctlOpcode, s.ctlIndex, (intptr_t) s.ctlValue, s.data, s.ctlOpt);
                        s.data[1023] = 0;
                        s.dataLength = (int32_t) std::strlen (s.data);
                        break;
                    }
                    case br::ptrStringIn:
                        s.data[br::dataSize - 1] = 0;
                        s.ctlResult = dispatch (s.ctlOpcode, s.ctlIndex, (intptr_t) s.ctlValue, s.data, s.ctlOpt);
                        break;
                    case br::ptrChunkOut:
                    {
                        void* chunk = nullptr;
                        const intptr_t size = dispatch (effGetChunk, s.ctlIndex, 0, &chunk, 0.0f);
                        if (chunk != nullptr && size > 0 && size <= br::dataSize)
                        {
                            std::memcpy (s.data, chunk, (size_t) size);
                            s.dataLength = (int32_t) size;
                            s.ctlResult = size;
                        }
                        else
                        {
                            s.dataLength = 0;
                            s.ctlResult = 0;
                        }
                        break;
                    }
                    case br::ptrChunkIn:
                        if (s.dataLength > 0 && s.dataLength <= br::dataSize)
                            s.ctlResult = dispatch (effSetChunk, s.ctlIndex, s.dataLength, s.data, 0.0f);
                        break;
                    default:
                        s.ctlResult = dispatch (s.ctlOpcode, s.ctlIndex, (intptr_t) s.ctlValue, nullptr, s.ctlOpt);
                        break;
                }
                if (s.ctlOpcode == effSetProgram || s.ctlKind == br::ptrChunkIn) shm->latency = effect->initialDelay;
                break;
            }

            case br::opGetParam:
                if (effect != nullptr && s.ctlIndex >= 0 && s.ctlIndex < effect->numParams)
                    s.ctlOpt = effect->getParameter (effect, s.ctlIndex);
                break;

            case br::opSetParam:
                if (effect != nullptr && s.ctlIndex >= 0 && s.ctlIndex < effect->numParams)
                    effect->setParameter (effect, s.ctlIndex, s.ctlOpt);
                break;

            case br::opGetAllParams:
                if (effect != nullptr)
                {
                    const int n = effect->numParams < br::dataSize / 4 ? effect->numParams : br::dataSize / 4;
                    auto* f = reinterpret_cast<float*> (s.data);
                    for (int i = 0; i < n; ++i) f[i] = effect->getParameter (effect, i);
                    s.dataLength = n * 4;
                    s.ctlResult = n;
                }
                break;

            case br::opPrepare:
                if (effect != nullptr)
                {
                    if (processing) { dispatch (effStopProcess); dispatch (effMainsChanged, 0, 0); }
                    sampleRate = s.ctlOpt > 0.0f ? s.ctlOpt : 48000.0;
                    blockSize = s.ctlIndex > 0 ? s.ctlIndex : 512;
                    dispatch (effSetSampleRate, 0, 0, nullptr, (float) sampleRate);
                    dispatch (effSetBlockSize, 0, blockSize);
                    dispatch (effSetProcessPrecision, 0, 0);
                    dispatch (effMainsChanged, 0, 1);
                    dispatch (effStartProcess);
                    processing = true;
                    shm->latency = effect->initialDelay;
                    s.ctlResult = 1;
                }
                break;

            case br::opSuspend:
                if (effect != nullptr && processing)
                {
                    dispatch (effStopProcess);
                    dispatch (effMainsChanged, 0, 0);
                    processing = false;
                }
                break;

            case br::opOpenEditor:  s.ctlResult = openEditor() ? 1 : 0; break;
            case br::opCloseEditor: closeEditor(); break;
            case br::opShowEditor:
                if (window != nullptr && editorOpen) { ShowWindow (window, SW_SHOWNORMAL); SetForegroundWindow (window); shm->editorOpen = 1; s.ctlResult = 1; }
                else s.ctlResult = openEditor() ? 1 : 0;
                break;
            case br::opQuit: quit = true; s.ctlResult = 1; break;
            default: break;
        }
        s.ctlDoneSeq = s.ctlSeq;
        SetEvent (ctlRep);
        return ! quit;
    }

    // ---- audio --------------------------------------------------------------------------------------
    void processAudio()
    {
        auto& s = *shm;
        const int n = s.numFrames < 0 ? 0 : (s.numFrames > br::maxFrames ? br::maxFrames : s.numFrames);
        midiOutCount = 0;

        if (effect != nullptr && processing && n > 0)
        {
            // time
            timeInfo = {};
            timeInfo.samplePos = s.samplePos;
            timeInfo.sampleRate = s.sampleRate > 0 ? s.sampleRate : sampleRate;
            timeInfo.ppqPos = s.ppqPos;
            timeInfo.tempo = s.tempo > 0 ? s.tempo : 120.0;
            timeInfo.barStartPos = s.barStartPos;
            timeInfo.cycleStartPos = s.cycleStartPos;
            timeInfo.cycleEndPos = s.cycleEndPos;
            timeInfo.timeSigNumerator = s.timeSigNum > 0 ? s.timeSigNum : 4;
            timeInfo.timeSigDenominator = s.timeSigDen > 0 ? s.timeSigDen : 4;
            timeInfo.flags = s.timeFlags;

            // parameters the host changed
            const int sets = s.numParamSets < br::maxParamSets ? s.numParamSets : br::maxParamSets;
            for (int i = 0; i < sets; ++i)
                if (s.paramSets[i].index >= 0 && s.paramSets[i].index < effect->numParams)
                    effect->setParameter (effect, s.paramSets[i].index, s.paramSets[i].value);

            // MIDI
            const int nm = s.numMidiIn < br::maxMidi ? s.numMidiIn : br::maxMidi;
            if (nm > 0)
            {
                auto* ev = reinterpret_cast<Events*> (eventBlock.data());
                for (int i = 0; i < nm; ++i)
                {
                    auto& me = midiEvents[(size_t) i];
                    me = {};
                    me.type = eventTypeMidi;
                    me.byteSize = (int32_t) sizeof (MidiEvent);
                    me.deltaFrames = s.midiIn[i].delta < 0 ? 0 : (s.midiIn[i].delta >= n ? n - 1 : s.midiIn[i].delta);
                    me.flags = 1;
                    std::memcpy (me.midiData, s.midiIn[i].bytes, 4);
                    ev->events[i] = reinterpret_cast<Event*> (&me);
                }
                ev->numEvents = nm;
                ev->reserved = 0;
                dispatch (effProcessEvents, 0, 0, ev);
            }

            // audio: separate input and output buffers, straight in the shared memory
            float* ins[br::maxChannels];
            float* outs[br::maxChannels];
            static float silence[br::maxFrames];
            const int nIn = effect->numInputs, nOut = effect->numOutputs;
            for (int c = 0; c < nIn && c < br::maxChannels; ++c)
                ins[c] = c < s.numIn ? s.audioIn + c * br::maxFrames : silence;
            for (int c = 0; c < nOut && c < br::maxChannels; ++c)
            {
                outs[c] = s.audioOut + c * br::maxFrames;
                std::memset (outs[c], 0, sizeof (float) * (size_t) n);
            }
            if (nIn <= br::maxChannels && nOut <= br::maxChannels)
            {
                if ((effect->flags & flagCanReplacing) != 0 && effect->processReplacing != nullptr)
                    effect->processReplacing (effect, ins, outs, n);
                else if (effect->processAccumulating != nullptr)
                    effect->processAccumulating (effect, ins, outs, n);
            }
            for (int c = nOut; c < br::maxChannels; ++c)
                std::memset (s.audioOut + c * br::maxFrames, 0, sizeof (float) * (size_t) n);

            // every parameter's value (the plugin's own GUI may have moved them)
            const int np = effect->numParams < br::maxParams ? effect->numParams : br::maxParams;
            for (int i = 0; i < np; ++i) s.paramValues[i] = effect->getParameter (effect, i);
            s.numParams = np;
        }
        else
        {
            std::memset (s.audioOut, 0, sizeof (float) * (size_t) br::maxChannels * (size_t) br::maxFrames);
        }
        s.numMidiOut = midiOutCount;
        s.audDoneSeq = s.audSeq;
        SetEvent (audRep);
    }

    DWORD WINAPI audioThread (void*)
    {
        onAudioThread = true;
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        HANDLE waits[] = { audReq, quitEvent };
        for (;;)
        {
            const DWORD r = WaitForMultipleObjects (2, waits, FALSE, INFINITE);
            if (r != WAIT_OBJECT_0) break;
            processAudio();
        }
        return 0;
    }

    HANDLE openEvent (const std::string& base, const char* suffix)
    {
        char name[300];
        br::objectName (name, sizeof (name), base.c_str(), suffix);
        return OpenEventA (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
    }
}

int WINAPI wWinMain (HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW (GetCommandLineW(), &argc);
    std::wstring dll, shmName;
    DWORD hostPid = 0;
    for (int i = 1; i + 1 < argc; ++i)
    {
        const std::wstring a = argv[i];
        if (a == L"--dll") dll = argv[++i];
        else if (a == L"--shm") shmName = argv[++i];
        else if (a == L"--host") hostPid = (DWORD) _wtoi (argv[++i]);
    }
    if (dll.empty() || shmName.empty()) return 2;
    const std::string base (shmName.begin(), shmName.end());

    HANDLE mapping = OpenFileMappingA (FILE_MAP_ALL_ACCESS, FALSE, base.c_str());
    if (mapping == nullptr) return 3;
    shm = static_cast<br::Shared*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (br::Shared)));
    if (shm == nullptr || shm->version != br::protocolVersion) return 4;
    ctlReq = openEvent (base, ".ctlReq");
    ctlRep = openEvent (base, ".ctlRep");
    audReq = openEvent (base, ".audReq");
    audRep = openEvent (base, ".audRep");
    if (ctlReq == nullptr || ctlRep == nullptr || audReq == nullptr || audRep == nullptr) return 5;
    hostProcess = hostPid != 0 ? OpenProcess (SYNCHRONIZE, FALSE, hostPid) : nullptr;
    quitEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
    shm->bridgePid = (int32_t) GetCurrentProcessId();

    // never let a plugin's crash box block anything: the host sees the process end and reports it
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // load from the plugin's own folder (old plugins often find their files relative to the working directory)
    const auto slash = dll.find_last_of (L"\\/");
    if (slash != std::wstring::npos) SetCurrentDirectoryW (dll.substr (0, slash).c_str());
    loadPlugin (dll);

    HANDLE audio = CreateThread (nullptr, 0, audioThread, nullptr, 0, nullptr);

    HANDLE waits[2] = { ctlReq, hostProcess };
    const DWORD numWaits = hostProcess != nullptr ? 2 : 1;
    bool running = true;
    while (running)
    {
        const DWORD r = MsgWaitForMultipleObjects (numWaits, waits, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0)
        {
            running = handleControl();
        }
        else if (numWaits == 2 && r == WAIT_OBJECT_0 + 1)
        {
            running = false;   // the app is gone
        }
        else
        {
            MSG msg;
            while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage (&msg);
                DispatchMessageW (&msg);
            }
        }
    }

    SetEvent (quitEvent);
    if (audio != nullptr) { WaitForSingleObject (audio, 2000); CloseHandle (audio); }
    closeEditor();
    if (effect != nullptr)
    {
        if (processing) { dispatch (effStopProcess); dispatch (effMainsChanged, 0, 0); }
        dispatch (effClose);
        effect = nullptr;
    }
    if (module != nullptr) FreeLibrary (module);
    UnmapViewOfFile (shm);
    CloseHandle (mapping);
    return 0;
}
