#pragma once

#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <atomic>

namespace wis
{

/** Zero-latency mono convolution for short impulse responses (speaker cabinets):
    the first 64 taps are convolved directly, the rest in 64-sample FFT partitions (which arrive exactly
    64 samples late, so the two parts line up). Kernels are prepared on any thread and swapped in on the
    audio thread with a short crossfade, without locks or allocation. */
class Convolver
{
public:
    static constexpr int B = 64;              // partition size = direct part length
    static constexpr int fftOrder = 7;        // 2B

    struct Kernel
    {
        std::vector<float> direct;                                    // B taps
        int peak = 0;                                                 // index of the biggest tap (arrival time)
        std::vector<std::vector<std::complex<float>>> partitions;     // tail, B taps each, as spectra (B + 1 bins)
        static std::unique_ptr<Kernel> make (const float* ir, int length);
    };

    Convolver();
    ~Convolver();

    /** Message thread / prepare: resets and installs a kernel immediately (no crossfade). */
    void reset (std::unique_ptr<Kernel> k);
    /** Any thread: offers a new kernel; it's picked up by the audio thread and crossfaded in. Returns false if one is already waiting. */
    bool offer (std::unique_ptr<Kernel> k);
    /** Call from a non-audio thread from time to time: frees kernels the audio thread has finished with. */
    void collectGarbage();
    bool isIdle() const { return pending.load() == nullptr; }
    /** Audio thread: arrival time of the current impulse response, in samples. */
    int currentPeak() const noexcept { return next != nullptr ? next->peak : current != nullptr ? current->peak : 0; }

    void process (float* data, int numSamples) noexcept;

private:
    void processBlockTail() noexcept;
    float convolveDirect (const Kernel& k) const noexcept;

    juce::dsp::FFT fft { fftOrder };
    Kernel* current = nullptr;
    Kernel* next = nullptr;          // fading in
    std::atomic<Kernel*> pending { nullptr }, retired { nullptr };
    int fadePos = 0;
    static constexpr int fadeLength = 2048;

    // input history
    std::vector<float> history;      // direct part ring (power of two)
    int histPos = 0;
    std::vector<float> blockIn, prevBlockIn, fftBuf;
    std::vector<std::vector<std::complex<float>>> fdl;   // spectra of past input blocks
    int fdlPos = 0, blockFill = 0;
    std::vector<float> tailOutCur, tailOutNext;           // tail output for the current block
    std::vector<std::complex<float>> acc;
    void computeTail (const Kernel& k, std::vector<float>& out) noexcept;
};

} // namespace wis
