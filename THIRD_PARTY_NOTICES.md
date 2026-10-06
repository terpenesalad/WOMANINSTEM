# Third-party software

WOMANINSTEM is built on these open-source projects. Thank you to their authors.

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8 | App framework, audio devices (ASIO/WASAPI), file decoding (MP3/FLAC/WAV/OGG), DSP building blocks | AGPL-3.0 (or commercial) |
| [Demucs v4 "htdemucs_6s"](https://github.com/facebookresearch/demucs) by Meta AI (Défossez et al.) | The neural network that separates the stems | MIT |
| [demucs.cpp](https://github.com/sevagh/demucs.cpp) by Sevag Hanssian | C++ inference for Demucs, and the converted model weights | MIT |
| [Neural Amp Modeler Core](https://github.com/sdatkinson/NeuralAmpModelerCore) by Steven Atkinson | Real-time playback of `.nam` amp captures | MIT |
| [Eigen](https://eigen.tuxfamily.org) | Linear algebra for Demucs and NAM | MPL-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) | Reading `.nam` files | MIT |
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) & Signalsmith Linear | Practice speed and transpose (time-stretch / pitch-shift) | MIT |
| [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) by Aleksey Vaneev | High quality sample-rate conversion | MIT |
| [ASIO SDK](https://www.steinberg.net/developers/) by Steinberg Media Technologies | Low-latency audio driver support on Windows | GPL-3.0 (or proprietary) |
| libFLAC, Ogg Vorbis (bundled in JUCE) | FLAC / OGG decoding and encoding | BSD-style |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) by Bernhard Schelling | SoundFont (`.sf2`) playback for the Studio's Sound Library | MIT |
| [GeneralUser GS](https://www.schristiancollins.com/generaluser.php) v2.0.3 by S. Christian Collins | The built-in Sound Library: 287 General MIDI instruments and drum kits (bundled in `sounds/`) | GeneralUser GS License v2.0 (free for any use, see `sounds/GeneralUser-GS-LICENSE.txt`) |
| VST3 SDK by Steinberg Media Technologies (bundled in JUCE) | Hosting VST3 instruments and effects in the Studio | BSD-style (the SDK subset bundled with JUCE) |
| [CLAP](https://github.com/free-audio/clap) by the free-audio community | Hosting CLAP instruments and effects | MIT |
| lilv, serd, sord, sratom, zix and the LV2 specification (bundled in JUCE) by David Robillard and contributors | Hosting LV2 instruments and effects | ISC |

VST (2.x) plugins are hosted through WOMANINSTEM's own implementation of the plugin interface; no Steinberg VST2 SDK code is included.

ASIO and VST are trademarks of Steinberg Media Technologies GmbH. CLAP is an open standard by the free-audio community.
HomeKeys 20 and the Rhythm Box are original designs inspired by 1980s home keyboards and drum machines; all their sounds
are synthesized by WOMANINSTEM (no samples from any hardware are used), and they are not affiliated with any manufacturer.

The separation model is downloaded from the
[demucs.cpp weights on Hugging Face](https://huggingface.co/datasets/Retrobear/demucs.cpp) (converted from Meta's
MIT-licensed Demucs checkpoints).

Amp captures (`.nam`), cabinet impulse responses, SoundFonts, samples and plugins you load are made by their respective
authors and are subject to their own licenses; apart from GeneralUser GS, none are bundled with WOMANINSTEM.
