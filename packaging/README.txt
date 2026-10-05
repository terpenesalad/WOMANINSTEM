WOMANINSTEM - split it. mute it. play it. record it.
=====================================================

Start
-----
Double-click WOMANINSTEM.exe. Keep the "models" and "sounds" folders next to it
(the AI model and the built-in instrument library).

Two modes, switched with the tabs at the top (Ctrl+1 / Ctrl+2):
  PLAY ALONG - split a song into stems, mute your part, jam through the amp rig.
  STUDIO     - a full multitrack recording studio: instruments, drummer, recording,
               piano roll, mixer, effects, VST3 plugins.
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
   strings, synths...), Audio: Guitar or Bass (records through Amp & Pedals), Microphone.
3. Library (left): Sounds = 287 instruments and drum kits, Drums = 18 grooves,
   Effects = EQ, compressor, reverb, delay and more, Songs = your split songs.
   Double-click an item, or drag it onto a track.
4. Arm a track (R button on the track), press R to record (1 bar count-in), Space to stop.
   Turn on Cycle (C) to record several takes over a section.
5. Double-click a clip to edit it (piano roll for MIDI, gain / fades / takes for audio).
6. Mixer (X): faders, pan, insert effects. Project > Export Mix or Export Stems when done.

Studio shortcuts
----------------
Space play/stop | R record | Enter go to start | , . back/forward a bar | C cycle | K metronome
Ctrl+Z / Ctrl+Y undo/redo | Ctrl+S save | Ctrl+E export | Ctrl+T split | Ctrl+D duplicate
Ctrl+C / Ctrl+V copy/paste | Del delete | M / S mute/solo track | E editor | X mixer | B library
Ctrl+K Musical Typing (Z/X octave, C/V velocity)

VST3 plugins: Project > Plugin Manager > Options > Scan. Plugins appear in the track menu,
the mixer's slots and the Library.

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
- Built-in amps: Clean Combo, Brit Crunch, Hot Lead, Bass Tube, Bass Modern, Flat/DI.
- Load NAM...: any Neural Amp Modeler capture (.nam). Thousands are free at https://www.tone3000.com
- Load IR...: any cabinet impulse response (.wav).
- Gate, Compressor, Drive (overdrive/distortion/fuzz), Studio EQ, Chorus, Delay, Reverb, Tuner.
- Save your own presets with "Save".

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
