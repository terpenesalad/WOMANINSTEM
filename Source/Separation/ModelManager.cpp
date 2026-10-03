#include "ModelManager.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace wis
{

const ModelSpec& modelSpec (ModelId id)
{
    static const ModelSpec specs[] = {
        { ModelId::sixStem,  "ggml-model-htdemucs-6s-f16.bin",          "Demucs v4 6-stem",              54'900'000 },
        { ModelId::ftDrums,  "ggml-model-htdemucs_ft_drums-4s-f16.bin",  "Demucs v4 fine-tuned (drums)",  84'000'000 },
        { ModelId::ftBass,   "ggml-model-htdemucs_ft_bass-4s-f16.bin",   "Demucs v4 fine-tuned (bass)",   84'000'000 },
        { ModelId::ftVocals, "ggml-model-htdemucs_ft_vocals-4s-f16.bin", "Demucs v4 fine-tuned (vocals)", 84'000'000 },
    };
    return specs[(int) id];
}

juce::File ModelManager::appDataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WOMANINSTEM");
    dir.createDirectory();
    return dir;
}

juce::File ModelManager::userModelsDirectory()
{
    auto dir = appDataDirectory().getChildFile ("models");
    dir.createDirectory();
    return dir;
}

juce::File ModelManager::findModel (ModelId id)
{
    const juce::String name (modelSpec (id).fileName);
    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    const juce::File candidates[] = {
        exeDir.getChildFile ("models").getChildFile (name),
        exeDir.getChildFile (name),
        userModelsDirectory().getChildFile (name),
    };

    for (auto& f : candidates)
        if (f.existsAsFile() && f.getSize() > 1'000'000)
            return f;

    return {};
}

juce::URL ModelManager::downloadUrl (ModelId id)
{
    return juce::URL ("https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/" + juce::String (modelSpec (id).fileName));
}

juce::String ModelManager::download (ModelId id,
                                     const std::function<void (juce::int64, juce::int64)>& progress,
                                     const std::function<bool()>& shouldContinue)
{
    auto& spec = modelSpec (id);
    auto target = userModelsDirectory().getChildFile (spec.fileName);
    auto temp   = target.getSiblingFile (target.getFileName() + ".part");
    temp.deleteFile();

    int statusCode = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (20000)
                       .withNumRedirectsToFollow (10)
                       .withStatusCode (&statusCode);

    auto stream = downloadUrl (id).createInputStream (options);

    if (stream == nullptr)
        return "Couldn't connect to huggingface.co to download the " + juce::String (spec.description)
             + " model. Check your internet connection, or download " + spec.fileName
             + " manually and put it in: " + userModelsDirectory().getFullPathName();

    if (statusCode != 0 && statusCode != 200)
        return "Model download failed (HTTP " + juce::String (statusCode) + ").";

    auto total = stream->getTotalLength();
    if (total <= 0) total = spec.approxBytes;

    {
        juce::FileOutputStream out (temp);
        if (! out.openedOk())
            return "Can't write to " + temp.getFullPathName();

        juce::HeapBlock<char> buffer (1 << 16);
        juce::int64 done = 0;

        for (;;)
        {
            if (shouldContinue && ! shouldContinue())
            {
                out.flush();
                temp.deleteFile();
                return "Cancelled";
            }

            auto n = stream->read (buffer, 1 << 16);
            if (n <= 0)
                break;

            out.write (buffer, (size_t) n);
            done += n;

            if (progress)
                progress (done, total);
        }

        out.flush();
    }

    // Sanity check: ggml demucs files start with "dmc6" or "dmc4" magic (little endian).
    {
        juce::FileInputStream in (temp);
        const auto magic = (juce::uint32) in.readInt();
        if (! (magic == 0x646d6336 || magic == 0x646d6334) || temp.getSize() < 10'000'000)
        {
            temp.deleteFile();
            return "The downloaded model file looks damaged. Please try again.";
        }
    }

    target.deleteFile();
    if (! temp.moveFileTo (target))
        return "Couldn't save the model to " + target.getFullPathName();

    return {};
}

std::string ModelManager::pathForFopen (const juce::File& f)
{
    auto full = f.getFullPathName();

   #if JUCE_WINDOWS
    bool ascii = true;
    for (auto c : full)
        if (c > 127) { ascii = false; break; }

    if (! ascii)
    {
        wchar_t shortPath[MAX_PATH * 2] = {};
        if (GetShortPathNameW (full.toWideCharPointer(), shortPath, (DWORD) std::size (shortPath)) > 0)
            return juce::String (shortPath).toStdString();
    }
   #endif

    return full.toStdString();
}

} // namespace wis
