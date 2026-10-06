#include "NativeWindow.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace wis::daw
{

bool NativeChildWindow::isSupported()
{
   #if JUCE_WINDOWS
    return true;
   #else
    return false;
   #endif
}

double NativeChildWindow::displayScale()
{
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::jmax (1.0, d->scale);
    return 1.0;
}

NativeChildWindow::NativeChildWindow()
{
   #if JUCE_WINDOWS
    // a plain popup to start with; HWNDComponent turns it into a child of our window once we're on screen
    auto hwnd = CreateWindowExW (0, L"STATIC", L"", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
    handle = hwnd;
    host.setHWND (hwnd);
    addAndMakeVisible (host);
   #endif
    setOpaque (true);
}

NativeChildWindow::~NativeChildWindow()
{
    destroy();
}

void NativeChildWindow::destroy()
{
   #if JUCE_WINDOWS
    host.setHWND (nullptr);   // destroys the window
   #endif
    handle = nullptr;
}

void NativeChildWindow::resized()
{
   #if JUCE_WINDOWS
    host.setBounds (getLocalBounds());
   #endif
}

} // namespace wis::daw
