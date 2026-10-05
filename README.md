<p align="center">
  <img src="Resources/icon.png" width="96" alt="">
</p>

<h1 align="center">WOMANINSTEM</h1>
<p align="center"><b>split it. mute it. play it. record it.</b></p>

<p align="center">
  Drop in any MP3 or FLAC → AI splits it into stems → mute your part → plug in your bass, guitar or mic and play along through a real amp rig.<br>
  Then take it into the <b>Studio</b>: a full multitrack recording studio with software instruments, a drummer, piano roll, mixer, effects and VST3 plugins.
</p>

<p align="center">
  <a href="../../releases/latest"><b>⬇ Download for Windows</b></a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#the-studio">The Studio</a> ·
  <a href="#building-from-source">Build from source</a>
</p>

The app has two modes, switched with the tabs at the top (or `Ctrl+1` / `Ctrl+2`):

| **PLAY ALONG** | **STUDIO** |
|---|---|
| ![Play Along](docs/screenshot.png) | ![Studio](docs/studio.png) |
| Split a song, mute your part, jam through the amp rig. | Write, record, arrange and mix your own songs. |

## Play Along

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

## The Studio

A GarageBand / Logic-style recording studio, built in. Everything runs at low latency on the same audio interface and the same amp rig.

![Studio mixer](docs/studio-mixer.png)

**Tracks**
- **Software Instrument** tracks, played from a **USB MIDI keyboard** (any class-compliant keyboard works, plug it in and go) or your computer keyboard (**Musical Typing**, `Ctrl+K`).
- **Sound Library**: 287 built-in instruments and drum kits (GeneralUser GS): grand & electric pianos, organs, guitars, basses, strings, choirs, brass, woodwinds, synths, percussion and 13 drum kits. Load any other `.sf2` SoundFont too.
- **Studio Synth**: a 16-voice analog-style synth (2 oscillators + sub + noise, resonant filter with envelope, LFO, glide / mono legato) with 16 presets.
- **Drummer**: 18 grooves played by a real kit (rock, pop, four-on-the-floor, disco, funk, Motown, boom bap, trap, half-time, shuffle, jazz swing, reggae, bossa nova, metal...) with crash cymbals and tom fills. Drop 8 bars in and edit any hit.
- **Audio** tracks for guitar, bass, vocals or anything else, mono or stereo inputs, with input monitoring.
- **Your Play-Along rig as a plugin**: *Amp & Pedals* puts the whole rig (gate, comp, drive, amp/NAM, cab/IR, EQ, chorus, delay, reverb, tuner) on any track.

**Recording**
- Record audio and MIDI on as many armed tracks as you like at once, with **1-bar count-in**, **metronome** and automatic **latency compensation**.
- **Cycle recording** stacks **takes**; pick the best one per clip later. MIDI cycle recording merges (overdubs) passes.
- Recordings are 24-bit WAV files inside the song's folder.

**Editing**
- Arrangement with snapping (bar / beat / 1/8 / 1/16 / auto), drag to move (`Alt` = copy), trim either edge non-destructively, fades, split at playhead, duplicate, repeat, copy/paste, marquee selection, rename, colour, clip gain, mute.
- **Piano roll**: draw, move, resize, transpose, velocity lane, quantize (straight or triplet grids), drum names for drum tracks, octave shift, duplicate.
- **Volume and pan automation** lanes.
- **Unlimited undo / redo**.

**Mixing**
- Mixer with channel strips: instrument slot, unlimited **insert effects** (with bypass), pan, fader, peak meters, mute / solo / record-arm, plus master inserts and master fader.
- 11 built-in effects with presets: **Channel EQ** (6 bands with a live curve), **Compressor** (with gain-reduction meter), **Limiter**, **Noise Gate**, **Reverb**, **Delay** (tempo-sync, ping-pong), **Chorus**, **Phaser**, **Tremolo / Auto-pan**, **Saturator**, **Amp & Pedals**.
- **VST3 plugins**: scan your VST3 folder (Project → Plugin Manager). Each plugin is tested in a separate process, so a crashy plugin can't take the app down.

**Songs and files**
- **Open in Studio** from Play Along: every stem on its own track, **tempo and downbeat detected** so the bar grid lines up, the stems you muted stay muted, and a track with your current rig, armed and ready.
- Drag in WAV / MP3 / FLAC / OGG / AIFF files or **MIDI files** (one track per channel, drums on channel 10 get a drum kit).
- **Export** the mix or **stems (one file per track)** as WAV (16/24/32-bit float), FLAC or OGG, at any sample rate, whole song or cycle region, optionally normalised. **Export MIDI** too.
- Songs live in `Documents\WOMANINSTEM Projects` (one folder per song with its audio files). Autosave every 2 minutes with crash recovery, and a `.backup` of the previous save.

### Studio shortcuts

| Key | | Key | |
|---|---|---|---|
| `Space` | play / stop | `R` | record |
| `Enter` | go to start | `,` / `.` | back / forward a bar |
| `C` | cycle on/off | `K` | metronome on/off |
| `Ctrl+K` | Musical Typing (`A`–`'` play notes, `W E T Y U O P` sharps, `Z`/`X` octave, `C`/`V` velocity) | `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+T` | split at playhead | `Ctrl+D` | duplicate |
| `Ctrl+C` / `Ctrl+V` | copy / paste at playhead | `Del` | delete |
| `M` / `S` | mute / solo the selected track | `↑` / `↓` | select track |
| `E` / `X` / `B` | editor / mixer / library | `Z` | zoom to fit |
| `Ctrl+S` | save | `Ctrl+E` | export |

Mouse: double-click empty space on an instrument track for a new MIDI clip, double-click a clip to edit it, drag in the ruler's top strip to set the cycle, `Ctrl` + mouse wheel to zoom.

## Quick start

### Play Along

1. Download the latest **[release](../../releases/latest)**: the `-setup.exe` installer or the portable `.zip`.
   *(SmartScreen may warn because the app isn't code-signed: **More info → Run anyway**.)*
2. **Audio & MIDI** (top right) → pick your interface's **ASIO** driver, **48 kHz**, **64–128 samples**. Enable the input you're plugged into.
3. In **YOUR RIG → INPUT**, choose that input and pick a preset. Strum: the meter moves and you hear yourself.
4. **Open Song** (or drag a file onto the window). The first split takes a few minutes; after that it's instant.
5. Set **I'm playing** to your instrument, press **Space**, and play.
6. Want to record a cover? Click **Open in Studio**.

### Studio

1. Click **STUDIO** at the top. A piano track is ready: play it with a MIDI keyboard or press `Ctrl+K` and use your computer keyboard.
2. **+ Track** → *Drummer* for a beat, *Software Instrument* for keys / bass / strings / synths, *Audio: Guitar or Bass* to record your instrument through the amp rig.
3. Pick sounds in the **Library** on the left (double-click, or drag onto a track). Grooves are under *Drums*, effects under *Effects*, your split songs under *Songs*.
4. Arm a track (**R** button), press **R** to record (one bar of count-in), **Space** to stop.
5. **Mixer** to balance it, **Project → Export Mix** when it's done.

| Play Along shortcut | |
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
- **Audio & MIDI** (top right) is where you choose your interface and turn on MIDI keyboards.
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
- **Sound Library**: put `GeneralUser-GS.sf2` from [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) in a `sounds/` folder next to the exe (or point `WIS_SOUNDFONT` at it).
- `rigtest` runs the DSP/engine test-suite, `dawtest` the Studio engine (timing, recording, instruments, effects, export, tempo detection), `WOMANINSTEM --selftest-ui` opens every plugin editor, and `stemsplit --selftest` checks separation end-to-end.

Every push builds and tests on Windows and Linux via GitHub Actions. **To publish a new release**, bump `VERSION` in the `project(...)` line of `CMakeLists.txt` and push to `main`: the Windows job builds the installer and portable zip and creates the GitHub Release `vX.Y.Z` automatically.

### Project layout

```
Source/
  Separation/   audio decoding, Demucs inference (multi-threaded), lead/backing vocal split, model download
  Library/      stem cache (FLAC + manifest), export
  Engine/       real-time audio callback, stem player (loop, time-stretch), recorder
  Rig/          amps, drive, cab sim, NAM host, effects, tuner, presets
  Daw/
    Model/      the song (tracks, clips, notes, automation; undo; save/load), drum grooves, MIDI files, tempo detection
    Engine/     real-time multitrack engine (lock-free snapshots), recording, metronome, export, audio file cache
    Plugins/    built-in effects, VST3 hosting with out-of-process scanning
    Instruments/ Sound Library (SoundFont) and Studio Synth
  UI/           app shell, look & feel, waveform lanes, mixer, pedalboard, overlays
    Studio/     control bar, library browser, arrangement, piano roll, mixer, plugin editors
Tools/          stemsplit (CLI), rigtest and dawtest (automated tests)
packaging/      installer script, end-user readme, release notes
```

## Honest limitations

- **Strings and horns** don't get their own stems: no open model separates them reliably yet, so they live in *Other*.
- **Backing vocals** are split from the lead by stereo position. This works well on most modern mixes, but not on mono recordings or songs where harmonies are panned centre.
- Separation is CPU-only, which keeps the app small (a few MB plus a 55 MB model) and runs on any PC, but it's slower than GPU-based tools.
- The Studio's tempo is constant per song (no tempo changes or time-stretching of audio clips yet), and it hosts VST3 only (no VST2, AU or CLAP).
- Tempo detection for **Open in Studio** assumes a steady 4/4 beat. If it's off, type the right tempo in the LCD: the stems stay where they are, only the grid moves.

## License

AGPL-3.0, required by JUCE's open-source license and the GPL-licensed ASIO SDK. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the brilliant open-source projects this is built on: Demucs, demucs.cpp, Neural Amp Modeler, JUCE, Eigen, Signalsmith Stretch, r8brain and TinySoundFont, plus S. Christian Collins' GeneralUser GS sound library.
