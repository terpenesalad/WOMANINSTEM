#include "StemSeparator.h"
#include "VocalSplitter.h"

#if defined(_MSC_VER)
 #pragma warning (push, 0)
#endif
#include <model.hpp>
#include <dsp.hpp>
#if defined(_MSC_VER)
 #pragma warning (pop)
#endif

#include <atomic>
#include <thread>

namespace wis
{

StemSeparator::StemSeparator() = default;
StemSeparator::~StemSeparator() = default;

int StemSeparator::defaultThreadCount()
{
    const int hw = (int) std::max (1u, std::thread::hardware_concurrency());
    // Leave one core for the UI / audio; Demucs workers are single threaded each.
    return juce::jlimit (1, 16, hw > 2 ? hw - 1 : hw);
}

juce::Array<ModelId> StemSeparator::missingModels (SeparationQuality q)
{
    juce::Array<ModelId> missing;
    juce::Array<ModelId> needed { ModelId::sixStem };
    if (q == SeparationQuality::maximum)
        needed.addArray (juce::Array<ModelId> { ModelId::ftDrums, ModelId::ftBass, ModelId::ftVocals });

    for (auto id : needed)
        if (! ModelManager::isInstalled (id))
            missing.add (id);
    return missing;
}

demucscpp::demucs_model* StemSeparator::getModel (ModelId id, juce::String& error)
{
    if (auto it = models.find (id); it != models.end())
        return it->second.get();

    auto file = ModelManager::findModel (id);
    if (! file.existsAsFile())
    {
        error = "Model file " + juce::String (modelSpec (id).fileName) + " is not installed.";
        return nullptr;
    }

    auto model = std::make_unique<demucscpp::demucs_model>();
    bool ok = false;

    try
    {
        ok = demucscpp::load_demucs_model (ModelManager::pathForFopen (file), model.get());
    }
    catch (const std::exception& e)
    {
        error = "Failed to load model: " + juce::String (e.what());
        return nullptr;
    }

    if (! ok)
    {
        error = "The model file " + file.getFullPathName() + " couldn't be loaded (it may be damaged - delete it and it will be downloaded again).";
        return nullptr;
    }

    auto* raw = model.get();
    models[id] = std::move (model);
    return raw;
}

bool StemSeparator::runModel (ModelId id, const juce::AudioBuffer<float>& mix, int numThreads,
                              float progressStart, float progressEnd, const juce::String& stage,
                              const Progress& progress, const std::function<bool()>& shouldCancel,
                              std::vector<juce::AudioBuffer<float>>& sourcesOut, juce::String& error)
{
    auto* model = getModel (id, error);
    if (model == nullptr)
        return false;

    const int numSources = model->is_4sources ? 4 : 6;
    const int total = mix.getNumSamples();

    // ---- split into overlapping chunks, one per worker ---------------------------------------
    const int overlap   = (int) (demucscpp::SUPPORTED_SAMPLE_RATE * 1.0);     // 1 s crossfade between chunks
    const int minChunk  = (int) (demucscpp::SUPPORTED_SAMPLE_RATE * 15.0);    // don't bother splitting tiny chunks
    const int workers   = juce::jlimit (1, juce::jmax (1, numThreads), juce::jmax (1, total / minChunk));
    const int coreLen   = (total + workers - 1) / workers;

    struct Chunk
    {
        int coreStart = 0, coreEnd = 0;   // the region this chunk "owns"
        int padStart = 0, padEnd = 0;     // the region actually processed (with overlap)
        Eigen::MatrixXf input;
        Eigen::Tensor3dXf output;
        bool failed = false;
        std::string failure;
    };

    std::vector<Chunk> chunks ((size_t) workers);
    for (int w = 0; w < workers; ++w)
    {
        auto& c = chunks[(size_t) w];
        c.coreStart = w * coreLen;
        c.coreEnd   = juce::jmin (total, c.coreStart + coreLen);
        c.padStart  = juce::jmax (0, c.coreStart - overlap);
        c.padEnd    = juce::jmin (total, c.coreEnd + overlap);

        const int len = c.padEnd - c.padStart;
        c.input.resize (2, len);

        auto* l = mix.getReadPointer (0, c.padStart);
        auto* r = mix.getReadPointer (1, c.padStart);

        // A fully silent chunk would make Demucs' normalisation divide by zero, so add inaudible dither.
        juce::Random rng ((juce::int64) w * 7919 + 17);
        for (int i = 0; i < len; ++i)
        {
            c.input (0, i) = l[i] + (rng.nextFloat() - 0.5f) * 2.0e-6f;
            c.input (1, i) = r[i] + (rng.nextFloat() - 0.5f) * 2.0e-6f;
        }
    }

    // ---- run workers ----------------------------------------------------------------------------
    std::vector<std::atomic<float>> workerProgress ((size_t) workers);
    for (auto& p : workerProgress) p.store (0.0f);

    std::atomic<bool> cancelFlag { false };
    std::vector<std::thread> threads;

    for (int w = 0; w < workers; ++w)
    {
        threads.emplace_back ([&, w]
        {
            auto& c = chunks[(size_t) w];

            demucscpp::ProgressCallback cb = [&, w] (float p, const std::string&)
            {
                workerProgress[(size_t) w].store (juce::jlimit (0.0f, 1.0f, p));
                if (cancelFlag.load())
                    throw CancelledException {};
            };

            try
            {
                c.output = demucscpp::demucs_inference (*model, c.input, cb);
                workerProgress[(size_t) w].store (1.0f);
            }
            catch (const CancelledException&) { c.failed = true; c.failure = "Cancelled"; }
            catch (const std::bad_alloc&)     { c.failed = true; c.failure = "Out of memory"; cancelFlag = true; }
            catch (const std::exception& e)   { c.failed = true; c.failure = e.what(); cancelFlag = true; }
            catch (...)                       { c.failed = true; c.failure = "Unknown error"; cancelFlag = true; }
        });
    }

    // Poll progress / cancellation from this (coordinating) thread.
    for (;;)
    {
        bool allDone = true;
        float sum = 0.0f;
        for (auto& p : workerProgress)
        {
            const float v = p.load();
            sum += v;
            allDone = allDone && v >= 1.0f;
        }

        bool anyFailed = false;
        for (auto& c : chunks) anyFailed = anyFailed || c.failed;

        if (progress)
            progress (progressStart + (progressEnd - progressStart) * (sum / (float) workers), stage);

        if (shouldCancel && shouldCancel())
            cancelFlag = true;

        if (allDone || anyFailed || cancelFlag.load())
            break;

        std::this_thread::sleep_for (std::chrono::milliseconds (150));
    }

    for (auto& t : threads)
        t.join();

    for (auto& c : chunks)
    {
        if (c.failed)
        {
            error = c.failure == "Out of memory"
                  ? juce::String ("Ran out of memory while separating. Close other programs, or lower the thread count in Settings.")
                  : juce::String (c.failure);
            return false;
        }
    }

    if (cancelFlag.load() || (shouldCancel && shouldCancel()))
    {
        error = "Cancelled";
        return false;
    }

    // ---- overlap-add the chunks back together with linear crossfades -----------------------------
    sourcesOut.clear();
    for (int s = 0; s < numSources; ++s)
    {
        sourcesOut.emplace_back (2, total);
        sourcesOut.back().clear();
    }

    std::vector<float> weightSum ((size_t) total, 0.0f);

    for (int w = 0; w < workers; ++w)
    {
        auto& c = chunks[(size_t) w];
        const int len = c.padEnd - c.padStart;
        const int fadeIn  = c.coreStart - c.padStart;   // 0 for the first chunk
        const int fadeOut = c.padEnd - c.coreEnd;       // 0 for the last chunk

        for (int i = 0; i < len; ++i)
        {
            float wgt = 1.0f;
            // ramps span 2x overlap centred on the chunk boundary
            if (fadeIn > 0 && i < 2 * fadeIn)
                wgt = juce::jmin (wgt, (float) (i + 1) / (float) (2 * fadeIn + 1));
            if (fadeOut > 0 && i >= len - 2 * fadeOut)
                wgt = juce::jmin (wgt, (float) (len - i) / (float) (2 * fadeOut + 1));

            const int g = c.padStart + i;
            weightSum[(size_t) g] += wgt;

            for (int s = 0; s < numSources; ++s)
            {
                sourcesOut[(size_t) s].getWritePointer (0)[g] += c.output (s, 0, i) * wgt;
                sourcesOut[(size_t) s].getWritePointer (1)[g] += c.output (s, 1, i) * wgt;
            }
        }

        c.output = Eigen::Tensor3dXf();   // free memory early
        c.input.resize (0, 0);
    }

    for (auto& buf : sourcesOut)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < total; ++i)
                if (weightSum[(size_t) i] > 0.0f)
                    d[i] /= weightSum[(size_t) i];
        }

    return true;
}

static float bufferRmsDb (const juce::AudioBuffer<float>& b)
{
    if (b.getNumSamples() == 0) return -120.0f;
    double sum = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            sum += (double) d[i] * d[i];
    }
    const double rms = std::sqrt (sum / (double) (b.getNumSamples() * b.getNumChannels()));
    return (float) juce::Decibels::gainToDecibels (rms, -120.0);
}

juce::String StemSeparator::separate (const juce::AudioBuffer<float>& mix,
                                      const SeparationSettings& settings,
                                      const Progress& progress,
                                      const std::function<bool()>& shouldCancel,
                                      SeparatedStems& result)
{
    if (mix.getNumChannels() != 2 || mix.getNumSamples() < 4410)
        return "The audio is too short to separate.";

    const int threads = settings.numThreads > 0 ? settings.numThreads : defaultThreadCount();
    const bool maxQuality = settings.quality == SeparationQuality::maximum;

    // Progress budget: 6-stem pass, optional 3 specialist passes, vocal split.
    const float sixStemEnd = maxQuality ? 0.25f : 0.92f;

    juce::String error;
    std::vector<juce::AudioBuffer<float>> six;
    if (! runModel (ModelId::sixStem, mix, threads, 0.0f, sixStemEnd, "Separating instruments",
                    progress, shouldCancel, six, error))
        return error;

    // demucs 6-source order: drums, bass, other, vocals, guitar, piano
    auto& out = result.audio;
    out[(size_t) StemId::drums]      = std::move (six[0]);
    out[(size_t) StemId::bass]       = std::move (six[1]);
    out[(size_t) StemId::other]      = std::move (six[2]);
    out[(size_t) StemId::leadVocals] = std::move (six[3]);
    out[(size_t) StemId::guitar]     = std::move (six[4]);
    out[(size_t) StemId::piano]      = std::move (six[5]);
    six.clear();

    if (maxQuality)
    {
        struct Pass { ModelId model; int sourceIndex; StemId stem; const char* label; };
        const Pass passes[] = {
            { ModelId::ftVocals, 3, StemId::leadVocals, "Refining vocals" },
            { ModelId::ftDrums,  0, StemId::drums,      "Refining drums" },
            { ModelId::ftBass,   1, StemId::bass,       "Refining bass" },
        };

        float start = sixStemEnd;
        const float step = (0.92f - sixStemEnd) / 3.0f;

        for (auto& p : passes)
        {
            std::vector<juce::AudioBuffer<float>> four;
            if (! runModel (p.model, mix, threads, start, start + step, p.label, progress, shouldCancel, four, error))
                return error;
            out[(size_t) p.stem] = std::move (four[(size_t) p.sourceIndex]);
            start += step;

            // Drop the specialist model from memory once used.
            models.erase (p.model);
        }

        // Keep the stems summing to the original mix: whatever the specialists didn't claim goes to "Other".
        auto& other = out[(size_t) StemId::other];
        other.makeCopyOf (mix);
        for (auto id : { StemId::drums, StemId::bass, StemId::leadVocals, StemId::guitar, StemId::piano })
            for (int ch = 0; ch < 2; ++ch)
                other.addFrom (ch, 0, out[(size_t) id], ch, 0, mix.getNumSamples(), -1.0f);
    }

    // Lead / backing vocal split by stereo image.
    auto& backing = out[(size_t) StemId::backingVocals];
    if (settings.splitBackingVocals)
    {
        if (progress) progress (0.93f, "Splitting lead and backing vocals");
        VocalSplitter::split (out[(size_t) StemId::leadVocals], out[(size_t) StemId::leadVocals], backing, shouldCancel);
        if (shouldCancel && shouldCancel())
            return "Cancelled";
    }
    else
    {
        backing.setSize (2, mix.getNumSamples());
        backing.clear();
    }

    if (progress) progress (0.98f, "Analysing stems");

    // A stem counts as present if it's loud enough relative to the mix.
    const float mixDb = bufferRmsDb (mix);
    for (int i = 0; i < numStemIds; ++i)
    {
        auto& b = out[(size_t) i];
        if (b.getNumSamples() != mix.getNumSamples())
        {
            b.setSize (2, mix.getNumSamples());
            b.clear();
        }
        result.rmsDb[(size_t) i]   = bufferRmsDb (b);
        result.present[(size_t) i] = result.rmsDb[(size_t) i] > juce::jmax (-60.0f, mixDb - 32.0f);
    }

    if (progress) progress (1.0f, "Done");
    return {};
}

} // namespace wis
