WOMANINSTEM - split it. mute it. play it.
==========================================

Start
-----
Double-click WOMANINSTEM.exe. Keep the "models" folder next to it (it holds the AI model).
No installation needed (or use the -setup.exe installer for Start Menu shortcuts).

If Windows SmartScreen says "Windows protected your PC": click "More info" -> "Run anyway".
(The app isn't code-signed - signing certificates cost money. The source code is public on GitHub.)

Quick start
-----------
1. Plug your bass, guitar or microphone into your USB audio interface.
2. Click "Audio Settings": choose your interface's ASIO driver (lowest latency),
   48000 Hz, buffer 64-128 samples. Turn on the input you're plugged into.
3. In YOUR RIG -> INPUT, choose that input. Pick a preset (e.g. "Bass - Vintage Tube").
   Strum: the input meter should move and you should hear yourself.
4. Click "Open Song" (or drag an MP3/FLAC onto the window).
   The song is split into drums, bass, guitar, keys, lead vocals, backing vocals and other.
   This takes a few minutes on your CPU the first time; after that it opens instantly from the Library.
5. "I'm playing: Bass" mutes the original bass. Press Space and play along.

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

Shortcuts
---------
Space play/pause | Home restart | Left/Right skip 5 s | L loop | R record | Ctrl+O open

Command line
------------
stemsplit.exe "song.mp3" "output folder" [--max]
Writes the stems as 24-bit WAV files.

Where things are stored
-----------------------
Library, presets, downloaded models: %APPDATA%\WOMANINSTEM
Recordings: Music\WOMANINSTEM Recordings

Licence: AGPL-3.0 (see LICENSE.txt). Third-party credits: THIRD_PARTY_NOTICES.txt
