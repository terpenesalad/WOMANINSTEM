#include "Convolver.h"

namespace wis
{

static constexpr int maxPartitions = 256;   // ~16k taps

std::unique_ptr<Convolver::Kernel> Convolver::Kernel::make (const float* ir, int length)
{
    auto k = std::make_unique<Kernel>();
    k->direct.assign ((size_t) B, 0.0f);
    for (int i = 0; i < juce::jmin (B, length); ++i) k->direct[(size_t) i] = ir[i];
    for (int i = 1; i < length; ++i) if (std::abs (ir[i]) > std::abs (ir[k->peak])) k->peak = i;

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buf ((size_t) (4 * B), 0.0f);
    const int tail = juce::jmax (0, length - B);
    const int parts = juce::jmin (maxPartitions, (tail + B - 1) / B);
    for (int p = 0; p < parts; ++p)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < B; ++i)
        {
            const int idx = B + p * B + i;
            if (idx < length) buf[(size_t) i] = ir[idx];
        }
        fft.performRealOnlyForwardTransform (buf.data(), true);
        std::vector<std::complex<float>> spec ((size_t) B + 1);
        for (int b = 0; b <= B; ++b) spec[(size_t) b] = { buf[(size_t) (2 * b)], buf[(size_t) (2 * b + 1)] };
        k->partitions.push_back (std::move (spec));
    }
    return k;
}

Convolver::Convolver()
{
    history.assign (2 * B, 0.0f);
    blockIn.assign (B, 0.0f);
    prevBlockIn.assign (B, 0.0f);
    fftBuf.assign ((size_t) (4 * B), 0.0f);
    fdl.assign (maxPartitions, std::vector<std::complex<float>> ((size_t) B + 1));
    tailOutCur.assign (B, 0.0f);
    tailOutNext.assign (B, 0.0f);
    acc.assign ((size_t) B + 1, {});
}

Convolver::~Convolver()
{
    delete current;
    delete next;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void Convolver::reset (std::unique_ptr<Kernel> k)
{
    delete current;
    delete next;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    current = k.release();
    next = nullptr;
    fadePos = 0;
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (blockIn.begin(), blockIn.end(), 0.0f);
    std::fill (prevBlockIn.begin(), prevBlockIn.end(), 0.0f);
    for (auto& s : fdl) std::fill (s.begin(), s.end(), std::complex<float>());
    std::fill (tailOutCur.begin(), tailOutCur.end(), 0.0f);
    std::fill (tailOutNext.begin(), tailOutNext.end(), 0.0f);
    histPos = fdlPos = blockFill = 0;
}

bool Convolver::offer (std::unique_ptr<Kernel> k)
{
    Kernel* expected = nullptr;
    auto* raw = k.get();
    if (pending.compare_exchange_strong (expected, raw)) { k.release(); return true; }
    return false;
}

void Convolver::collectGarbage()
{
    delete retired.exchange (nullptr);
}

float Convolver::convolveDirect (const Kernel& k) const noexcept
{
    float y = 0.0f;
    const int mask = 2 * B - 1;
    const float* h = k.direct.data();
    for (int i = 0; i < B; ++i)
        y += h[i] * history[(size_t) ((histPos - i) & mask)];
    return y;
}

void Convolver::computeTail (const Kernel& k, std::vector<float>& out) noexcept
{
    const int parts = (int) k.partitions.size();
    if (parts == 0) { std::fill (out.begin(), out.end(), 0.0f); return; }
    std::fill (acc.begin(), acc.end(), std::complex<float>());
    for (int p = 0; p < parts; ++p)
    {
        const auto& X = fdl[(size_t) ((fdlPos - p) & (maxPartitions - 1))];
        const auto& H = k.partitions[(size_t) p];
        for (int b = 0; b <= B; ++b) acc[(size_t) b] += X[(size_t) b] * H[(size_t) b];
    }
    for (int b = 0; b <= B; ++b) { fftBuf[(size_t) (2 * b)] = acc[(size_t) b].real(); fftBuf[(size_t) (2 * b + 1)] = acc[(size_t) b].imag(); }
    fft.performRealOnlyInverseTransform (fftBuf.data());
    for (int j = 0; j < B; ++j) out[(size_t) j] = fftBuf[(size_t) (B + j)];
}

void Convolver::processBlockTail() noexcept
{
    // spectrum of [previous block, this block] into the frequency-domain delay line
    fdlPos = (fdlPos + 1) & (maxPartitions - 1);
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    std::copy (prevBlockIn.begin(), prevBlockIn.end(), fftBuf.begin());
    std::copy (blockIn.begin(), blockIn.end(), fftBuf.begin() + B);
    fft.performRealOnlyForwardTransform (fftBuf.data(), true);
    auto& X = fdl[(size_t) fdlPos];
    for (int b = 0; b <= B; ++b) X[(size_t) b] = { fftBuf[(size_t) (2 * b)], fftBuf[(size_t) (2 * b + 1)] };
    std::copy (blockIn.begin(), blockIn.end(), prevBlockIn.begin());

    // a new kernel waiting? start crossfading to it (only at block boundaries, so its tail is complete)
    if (next == nullptr)
        if (auto* p = pending.exchange (nullptr))
        {
            next = p;
            fadePos = 0;
        }

    if (current != nullptr) computeTail (*current, tailOutCur);
    if (next != nullptr) computeTail (*next, tailOutNext);
}

void Convolver::process (float* data, int n) noexcept
{
    const int mask = 2 * B - 1;
    for (int i = 0; i < n; ++i)
    {
        const float x = data[i];
        histPos = (histPos + 1) & mask;
        history[(size_t) histPos] = x;
        blockIn[(size_t) blockFill] = x;

        float y = current != nullptr ? convolveDirect (*current) + tailOutCur[(size_t) blockFill] : x;
        if (next != nullptr)
        {
            const float yn = convolveDirect (*next) + tailOutNext[(size_t) blockFill];
            const float a = (float) fadePos / (float) fadeLength;
            y += (yn - y) * a;
            if (fadePos < fadeLength) ++fadePos;
        }
        data[i] = y;

        if (++blockFill == B)
        {
            blockFill = 0;
            // fade finished: the new kernel becomes current (the old one is freed by collectGarbage)
            if (next != nullptr && fadePos >= fadeLength && retired.load() == nullptr)
            {
                retired = current;
                current = next;
                next = nullptr;
            }
            processBlockTail();
        }
    }
}

} // namespace wis
