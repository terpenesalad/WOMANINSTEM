#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace wis::daw
{

/** A blank native child window that a plugin can draw its own editor into (VST2 / CLAP).
    Windows only; elsewhere isSupported() is false and the generic parameter editor is used instead. */
class NativeChildWindow : public juce::Component
{
public:
    NativeChildWindow();
    ~NativeChildWindow() override;

    static bool isSupported();
    /** HWND to give to the plugin (valid from construction). */
    void* getNativeHandle() const { return handle; }
    /** Scale between physical pixels (what plugins talk in) and component pixels. */
    static double displayScale();

    /** Destroys the native window (call after the plugin has closed its editor). */
    void destroy();

    void resized() override;

private:
    void* handle = nullptr;
   #if JUCE_WINDOWS
    juce::HWNDComponent host;
   #endif
};

} // namespace wis::daw
