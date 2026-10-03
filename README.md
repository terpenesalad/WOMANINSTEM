<p align="center">
  <img src="Resources/icon.png" width="96" alt="">
</p>

<h1 align="center">WOMANINSTEM</h1>
<p align="center"><b>split it. mute it. play it.</b></p>

<p align="center">
  Drop in any MP3 or FLAC → AI splits it into stems → mute your part → plug in your bass, guitar or mic and play along through a real amp rig.
</p>

<p align="center">
  <a href="../../releases/latest"><b>⬇ Download for Windows</b></a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#building-from-source">Build from source</a>
</p>

![WOMANINSTEM](docs/screenshot.png)

## What it does

**Stem separation**: Meta's Demucs v4 (`htdemucs_6s`) runs locally on your CPU. No upload, no account, no subscription.

| Stem | Notes |
|---|---|
| Drums | kit and percussion |
| Bass | bass guitar, synth bass |
| Guitar | electric and acoustic |
| Keys / Piano | piano and most keyboard parts |
| Lead Vocals | the centre-panned main vocal |
| Backing Vocals | wide / doubled / harmony vocals, split from the lead by stereo position |
| Other | everything else: strings, horns, synths, pads, FX |

Stems are cached in your **Library** (24-bit FLAC), so a song is only ever split once. You can export any song's stems as WAV for your DAW.

**Play-along rig**: plug into any USB audio interface (ASIO, WASAPI exclusive/shared) and play through:

- **Input** with channel picker, gain, clip warning and a **chromatic tuner** (down to low B on a 5-string)
- **Noise gate** (hysteresis + hold, no chatter) and **compressor**
- **Drive pedal**: overdrive / distortion / fuzz, 2× oversampled
- **Amp**: 6 built-in voicings (Clean Combo, Brit Crunch, Hot Lead, Bass Tube, Bass Modern, Flat/DI), 4× oversampled triode stages, tone stack, presence, power-amp sag. **Or load any [Neural Amp Modeler](https://www.neuralampmodeler.com/) `.nam` capture** (thousands free on [TONE3000](https://www.tone3000.com))
- **Cabinet**: 5 built-in speaker models or **any impulse response** (`.wav`), with low/high cut
- **Studio EQ, chorus, stereo/ping-pong delay, reverb** with pre-delay
- 10 factory presets (guitar, bass, vocal mic, acoustic/keys), all loudness-matched. Save your own.

**Practice tools**: mute/solo/volume/balance per stem · one-click *"I'm playing: Bass"* · drag to loop a section · slow down to 50% without pitch change · transpose ±12 semitones · record yourself as **Mix + Rig + dry DI** WAV files (re-amp the DI later) · safety limiter on the output.

## Quick start

1. Download the latest **[release](../../releases/latest)**: the `-setup.exe` installer or the portable `.zip`.
   *(SmartScreen may warn because the app isn't code-signed: **More info → Run anyway**.)*
2. **Audio Settings** → pick your interface's **ASIO** driver, **48 kHz**, **64–128 samples**. Enable the input you're plugged into.
3. In **YOUR RIG → INPUT**, choose that input and pick a preset. Strum: the meter moves and you hear yourself.
4. **Open Song** (or drag a file onto the window). The first split takes a few minutes; after that it's instant.
5. Set **I'm playing** to your instrument, press **Space**, and play.

| Shortcut | |
|---|---|
| `Space` | play / pause |
| `Home` | back to start (or loop start) |
| `←` / `→` | skip 5 s |
| `L` | loop on/off (drag across the waveform to set it, double-click to clear) |
| `R` | record |
| `Ctrl+O` | open a song |

### How long does separation take?
It runs on your CPU, so it depends on your computer. Measured on GitHub's 4-core cloud server, a **3-minute song took about 5 minutes**. A modern 8-core desktop with 16 GB of RAM should be roughly 2–3× faster. Each worker thread needs about 1.2 GB of RAM, so the app picks the thread count from your cores *and* your memory (8 GB machines use 2 workers). *Max quality* adds three fine-tuned models (vocals, drums, bass) and takes about 4× longer. Songs are only split once; after that they open instantly from the Library, and you can tune up and noodle on the rig while you wait.

### Latency tips
- Use your interface's **ASIO** driver (Focusrite, Audient, PreSonus, MOTU, Behringer and others all ship one), or *Windows Audio (Exclusive Mode)*.
- 64 or 128 samples at 48 kHz gives roughly 4–8 ms round trip. The current estimate is shown bottom-right.
- Using a `.nam` capture? Set the interface to the capture's sample rate (usually 48 kHz). The app tells you if they differ.

## Command line

`stemsplit.exe` ships alongside the app and uses the same engine:

```
stemsplit "song.flac" "out folder"            # 7 stems as 24-bit WAV
stemsplit "song.mp3" "out folder" --max       # max quality (downloads extra models once)
stemsplit --selftest out                      # end-to-end check on a synthetic song
```

## Building from source

Requirements: CMake ≥ 3.24, Visual Studio 2022 (Windows) or GCC/Clang (Linux/macOS). All libraries are fetched automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

- **ASIO**: download the [Steinberg ASIO SDK](https://www.steinberg.net/developers/) and add `-DWIS_ASIO_SDK_DIR=path/to/ASIOSDK`.
- **Model**: put `ggml-model-htdemucs-6s-f16.bin` in a `models/` folder next to the exe, or let the app download it on first use.
- `rigtest` runs the DSP/engine test-suite; `stemsplit --selftest` checks separation end-to-end.

Every push builds and tests on Windows and Linux via GitHub Actions. **To publish a new release**, bump `VERSION` in the `project(...)` line of `CMakeLists.txt` and push to `main`: the Windows job builds the installer and portable zip and creates the GitHub Release `vX.Y.Z` automatically.

### Project layout

```
Source/
  Separation/   audio decoding, Demucs inference (multi-threaded), lead/backing vocal split, model download
  Library/      stem cache (FLAC + manifest), export
  Engine/       real-time audio callback, stem player (loop, time-stretch), recorder
  Rig/          amps, drive, cab sim, NAM host, effects, tuner, presets
  UI/           look & feel, waveform lanes, mixer, pedalboard, overlays
Tools/          stemsplit (CLI) and rigtest (automated tests)
packaging/      installer script, end-user readme, release notes
```

## Honest limitations

- **Strings and horns** don't get their own stems: no open model separates them reliably yet, so they live in *Other*.
- **Backing vocals** are split from the lead by stereo position. This works well on most modern mixes, but not on mono recordings or songs where harmonies are panned centre.
- Separation is CPU-only, which keeps the app small (a few MB plus a 55 MB model) and runs on any PC, but it's slower than GPU-based tools.

## License

AGPL-3.0, required by JUCE's open-source license and the GPL-licensed ASIO SDK. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the brilliant open-source projects this is built on: Demucs, demucs.cpp, Neural Amp Modeler, JUCE, Eigen, Signalsmith Stretch and r8brain.
