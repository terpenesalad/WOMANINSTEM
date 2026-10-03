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
#include <mutex>
#include <thread>

namespace wis
{

StemSeparator::StemSeparator() = default;
StemSeparator::~StemSeparator() = default;

int StemSeparator::defaultThreadCount()
{
    const int hw = (int) std::max (1u, std::thread::hardware_concurrency());
    // Leave one core for the UI / audio; Demucs workers are single threaded each.
    const int byCores = juce::jlimit (1, 16, hw > 2 ? hw - 1 : hw);

    // Each worker needs its own working memory; never plan to use more than ~60% of the RAM.
    const int ramMB = juce::SystemStats::getMemorySizeInMegabytes();
    const int byRam = ramMB > 0 ? juce::jmax (1, (int) ((ramMB * 0.6 - baseMemoryMB) / perWorkerMemoryMB)) : byCores;

    return juce::jmin (byCores, byRam);
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

    // ---- work queue of overlapping ~30 s chunks ------------------------------------------------
    // Short chunks keep each worker's memory small and balance the load; each finished chunk is
    // cross-faded straight into the output and freed.
    const int overlap   = (int) (demucscpp::SUPPORTED_SAMPLE_RATE * 1.0);     // 1 s crossfade either side
    const int chunkLen  = (int) (demucscpp::SUPPORTED_SAMPLE_RATE * 30.0);
    const int numChunks = juce::jmax (1, (total + chunkLen - 1) / chunkLen);
    const int coreLen   = (total + numChunks - 1) / numChunks;                // equalise chunk sizes
    const int workers   = juce::jlimit (1, numChunks, juce::jmax (1, numThreads));

    sourcesOut.clear();
    for (int s = 0; s < numSources; ++s)
    {
        sourcesOut.emplace_back (2, total);
        sourcesOut.back().clear();
    }
    std::vector<float> weightSum ((size_t) total, 0.0f);
    std::mutex accumulateLock;

    std::vector<std::atomic<float>> chunkProgress ((size_t) numChunks);
    for (auto& p : chunkProgress) p.store (0.0f);

    std::atomic<int> nextChunk { 0 };
    std::atomic<bool> cancelFlag { false }, failed { false };
    std::atomic<int> activeWorkers { workers };
    std::mutex failureLock;
    juce::String failure;

    auto setFailure = [&] (const juce::String& f)
    {
        const std::lock_guard<std::mutex> l (failureLock);
        if (failure.isEmpty()) failure = f;
        failed = true;
        cancelFlag = true;
    };

    auto processChunk = [&] (int idx)
    {
        const int coreStart = idx * coreLen;
        const int coreEnd   = juce::jmin (total, coreStart + coreLen);
        const int padStart  = juce::jmax (0, coreStart - overlap);
        const int padEnd    = juce::jmin (total, coreEnd + overlap);
        const int len = padEnd - padStart;

        Eigen::MatrixXf input (2, len);
        auto* l = mix.getReadPointer (0, padStart);
        auto* r = mix.getReadPointer (1, padStart);

        // A fully silent chunk would make Demucs' normalisation divide by zero, so add inaudible dither.
        juce::Random rng ((juce::int64) idx * 7919 + 17);
        for (int i = 0; i < len; ++i)
        {
            input (0, i) = l[i] + (rng.nextFloat() - 0.5f) * 2.0e-6f;
            input (1, i) = r[i] + (rng.nextFloat() - 0.5f) * 2.0e-6f;
        }

        demucscpp::ProgressCallback cb = [&, idx] (float p, const std::string&)
        {
            chunkProgress[(size_t) idx].store (juce::jlimit (0.0f, 0.99f, p));
            if (cancelFlag.load())
                throw CancelledException {};
        };

        Eigen::Tensor3dXf output = demucscpp::demucs_inference (*model, input, cb);
        input.resize (0, 0);

        const int fadeIn  = coreStart - padStart;   // 0 for the first chunk
        const int fadeOut = padEnd - coreEnd;       // 0 for the last chunk

        const std::lock_guard<std::mutex> lock (accumulateLock);
        for (int i = 0; i < len; ++i)
        {
            float wgt = 1.0f;
            // linear ramps spanning 2x overlap, centred on the boundary -> neighbours sum to 1
            if (fadeIn > 0 && i < 2 * fadeIn)
                wgt = juce::jmin (wgt, (float) (i + 1) / (float) (2 * fadeIn + 1));
            if (fadeOut > 0 && i >= len - 2 * fadeOut)
                wgt = juce::jmin (wgt, (float) (len - i) / (float) (2 * fadeOut + 1));

            const int g = padStart + i;
            weightSum[(size_t) g] += wgt;
            for (int s = 0; s < numSources; ++s)
            {
                sourcesOut[(size_t) s].getWritePointer (0)[g] += output (s, 0, i) * wgt;
                sourcesOut[(size_t) s].getWritePointer (1)[g] += output (s, 1, i) * wgt;
            }
        }
        chunkProgress[(size_t) idx].store (1.0f);
    };

    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w)
    {
        threads.emplace_back ([&]
        {
            for (;;)
            {
                const int idx = nextChunk.fetch_add (1);
                if (idx >= numChunks || cancelFlag.load())
                    break;

                try                               { processChunk (idx); }
                catch (const CancelledException&) { break; }
                catch (const std::bad_alloc&)     { setFailure ("Out of memory"); break; }
                catch (const std::exception& e)   { setFailure (e.what()); break; }
                catch (...)                       { setFailure ("Unknown error"); break; }
            }
            --activeWorkers;
        });
    }

    // Poll progress / cancellation from this (coordinating) thread.
    while (activeWorkers.load() > 0)
    {
        float sum = 0.0f;
        for (auto& p : chunkProgress) sum += p.load();

        if (progress)
            progress (progressStart + (progressEnd - progressStart) * (sum / (float) numChunks), stage);

        if (shouldCancel && shouldCancel())
            cancelFlag = true;

        std::this_thread::sleep_for (std::chrono::milliseconds (150));
    }

    for (auto& t : threads)
        t.join();

    if (failed.load())
    {
        error = failure == "Out of memory"
              ? juce::String ("Ran out of memory while separating. Close other programs and try again.")
              : failure;
        return false;
    }

    if (cancelFlag.load() || (shouldCancel && shouldCancel()))
    {
        error = "Cancelled";
        return false;
    }

    size_t badSamples = 0;
    for (auto& buf : sourcesOut)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < total; ++i)
            {
                if (weightSum[(size_t) i] > 0.0f)
                    d[i] /= weightSum[(size_t) i];
                if (! std::isfinite (d[i])) { d[i] = 0.0f; ++badSamples; }
            }
        }

    // A handful of bad samples can be patched; lots means the model output is broken.
    if (badSamples > (size_t) total / 100)
    {
        error = "The separation produced invalid audio (" + juce::String ((juce::int64) badSamples)
              + " bad samples). Please report this on GitHub along with your CPU model.";
        return false;
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
