WOMANINSTEM - split it. mute it. play it. record it.
=====================================================

Start
-----
Double-click WOMANINSTEM.exe. Keep the "models" and "sounds" folders next to it
(the AI model and the built-in instrument library).

Two modes, switched with the tabs at the top (Ctrl+1 / Ctrl+2):
  PLAY ALONG - split a song into stems, mute your part, jam through the amp rig.
  STUDIO     - a full multitrack recording studio: instruments, drummer, 80s keyboard with
               rhythm box, samplers, arpeggiator, looper, recording, piano roll, mixer with
               buses / sends / side-chain, Vocal Tune, time-stretching, VST3 / VST / CLAP / LV2 plugins.
No installation needed (or use the -setup.exe installer for Start Menu shortcuts).

If Windows SmartScreen says "Windows protected your PC": click "More info" -> "Run anyway".
(The app isn't code-signed - signing certificates cost money. The source code is public on GitHub.)

Play Along quick start
----------------------
1. Plug your bass, guitar or microphone into your USB audio interface.
2. Click "Audio & MIDI" (top right): choose your interface's ASIO driver (lowest latency),
   48000 Hz, buffer 64-128 samples. Turn on the input you're plugged into.
3. In YOUR RIG -> INPUT, choose that input. Pick a preset (e.g. "Bass - Vintage Tube").
   Strum: the input meter should move and you should hear yourself.
4. Click "Open Song" (or drag an MP3/FLAC onto the window).
   The song is split into drums, bass, guitar, keys, lead vocals, backing vocals and other.
   This takes a few minutes on your CPU the first time; after that it opens instantly from the Library.
5. "I'm playing: Bass" mutes the original bass. Press Space and play along.
6. "Open in Studio" turns the song into a multitrack project (one track per stem,
   tempo detected) with a track ready for you to record a cover.

Studio quick start
------------------
1. Click STUDIO. A piano track is ready: play it with a USB MIDI keyboard, or press
   Ctrl+K (Musical Typing) and use A S D F G H J K L (white keys) / W E T Y U O P (black keys).
2. "+ Track": Drummer (a kit plus 8 bars of groove), Software Instrument (piano, keys,
   strings, synths...), HomeKeys 20 (80s keyboard with a 20-rhythm rhythm box), Vintage Rhythm
   Box, Sampler, Drum Pads, Loop Station, Beat Lab (groovebox), Audio: Guitar or Bass (records through Amp & Pedals),
   Microphone, and Aux Buses (a shared reverb or delay your tracks send to).
3. Library (left): Sounds = instruments and drum kits, Drums = 18 grooves + 20 vintage rhythms,
   Loops = chords / bass lines / arpeggios in your song's key, FX = effects and MIDI effects,
   Songs = your split songs. Double-click an item, or drag it onto a track.
4. Arm a track (R button on the track), press R to record (1 bar count-in), Space to stop.
   Turn on Cycle (C) to record several takes over a section.
5. Double-click a clip to edit it (piano roll for MIDI, gain / fades / takes for audio).
6. Mixer (X): faders, pan, insert effects, "+ MIDI FX" (arpeggiator...) above an instrument,
   "+ Send" to a bus, "Out:" to route a track into a bus. Right-click a compressor / gate /
   vocoder to pick its side-chain key track. Project > Export Mix or Export Stems when done.
7. Song key: click KEY in the display. Scale Lock, Vocal Tune and the Loops follow it.

Audio clips: Ctrl+drag a clip's right edge to time-stretch it. Right-click a clip for
Time & Pitch (follow song tempo, speed, transpose), Reverse, Normalize, Convert to Sampler Track.
Automation: track menu > Show Automation, then right-click the lane to automate volume, pan,
sends or any plugin knob. Right-click the ruler to add markers (Verse, Chorus...).

Studio shortcuts
----------------
Space play/stop | R record | Enter go to start | , . back/forward a bar | C cycle | K metronome
Ctrl+Z / Ctrl+Y undo/redo | Ctrl+S save | Ctrl+E export | Ctrl+T split | Ctrl+D duplicate
Ctrl+C / Ctrl+V copy/paste | Del delete | M / S mute/solo track | E editor | X mixer | B library
Ctrl+K Musical Typing (Z/X octave, C/V velocity)

Plugins (VST3, VST, CLAP, LV2): Project > Plugin Manager > Options > Scan. Plugins appear in
the track menu, the mixer's slots and the Library.

Practice tools
--------------
- Drag across the waveform to loop a section (double-click to clear). L toggles the loop.
- Speed: slow tricky parts down without changing the pitch.
- Key: transpose the song (e.g. -1 for songs in Eb tuning).
- M / S on each stem: mute / solo. Volume and left-right balance per stem.
- Rec (or R): records three WAV files to Music\WOMANINSTEM Recordings:
  Mix (what you hear), Rig (your processed sound), DI (your dry signal - re-amp it later).

Tone
----
- Presets: 22 finished sounds. Bass: 60s Merseybeat (violin bass), Late 60s Studio (DI + amp),
  Motown Flatwound, Classic Rock 8x10, Modern Growl, Modern Clean Hi-Fi, Punk Pick, Dub Deep,
  Fuzz Bass, Clean DI. Guitar: clean, jangle, blues, crunch, high gain, lead, dream pop, fuzz.
- Amps: American Clean, Tweed Breakup, British Chime, British Crunch, British Lead, Modern High
  Gain, Smooth Overdrive; Bass: 60s British Valve, Classic Tube 8x10, Vintage Flip-Top, Modern
  Growl, Studio DI; Flat/DI.
- Cabinet: 11 cabinets, microphone (dynamic / ribbon / condenser / both), Mic Pos (centre to
  edge = brighter to darker), Room, DI Blend (mixes in your clean bass, phase-aligned).
- Strings & Pickups: flatwounds, 60s violin bass, foam mute, roundwounds, P-bass, single coils,
  humbuckers. Tape: warm saturation.
- Load NAM...: any Neural Amp Modeler capture (.nam). Thousands are free at https://www.tone3000.com
- Load IR...: any cabinet impulse response (.wav).
- Gate, Compressor, Drive (overdrive/distortion/fuzz), Studio EQ, Chorus, Delay, Reverb, Tuner.
- Save your own presets with "Save".

Beat Lab (groovebox)
--------------------
"+ Track > Beat Lab". Press Play in it (or play the song) and click steps to make a beat.
- Drag samples or loops onto a lane name. Loops are chopped across the steps and stay in time.
- Trig / Velocity / Ratchet / Pitch / Chance / Nudge: what dragging a step changes.
  Right-click a step for rolls, chance, pitch, reverse; right-click a lane for Euclidean
  rhythms, randomise, length (odd lengths = polymeter) and loading samples.
- A-H: 8 patterns (Shift-click copies the current one). Chain plays them in turn.
- Mutate / Chaos / Break Shuffle: the glitch section. Hold the Perform pads for stutters,
  tape stop, reverse and fills; drag the XY pad for a filter sweep.
- Pattern to Song puts the pattern into the song as a MIDI clip.

Play Along shortcuts
--------------------
Space play/pause | Home restart | Left/Right skip 5 s | L loop | R record | Ctrl+O open

Command line
------------
stemsplit.exe "song.mp3" "output folder" [--max]
Writes the stems as 24-bit WAV files.

Where things are stored
-----------------------
Library, presets, downloaded models, plugin list: %APPDATA%\WOMANINSTEM
Play Along recordings: Music\WOMANINSTEM Recordings
Studio songs: Documents\WOMANINSTEM Projects (one folder per song, with its audio)

Licence: AGPL-3.0 (see LICENSE.txt). Third-party credits: THIRD_PARTY_NOTICES.txt
Sound library: GeneralUser GS by S. Christian Collins (sounds\GeneralUser-GS-LICENSE.txt)
