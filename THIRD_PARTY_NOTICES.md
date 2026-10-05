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

ASIO and VST are trademarks of Steinberg Media Technologies GmbH.

The separation model is downloaded from the
[demucs.cpp weights on Hugging Face](https://huggingface.co/datasets/Retrobear/demucs.cpp) (converted from Meta's
MIT-licensed Demucs checkpoints).

Amp captures (`.nam`), cabinet impulse responses, SoundFonts and VST3 plugins you load are made by their respective
authors and are subject to their own licenses; apart from GeneralUser GS, none are bundled with WOMANINSTEM.
