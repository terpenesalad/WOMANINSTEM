## WOMANINSTEM 3.0 for Windows (64-bit): the Studio grows up

**Download one of these:**
- `WOMANINSTEM-x.y.z-setup.exe`: installer with Start Menu / desktop shortcuts, "Open with WOMANINSTEM" for MP3/FLAC, and double-click to open Studio songs (`.wisproj`).
- `WOMANINSTEM-x.y.z-windows-x64.zip`: portable. Unzip anywhere and run `WOMANINSTEM.exe`.

Windows SmartScreen may warn because the app isn't code-signed: click **More info → Run anyway**.
Songs made in 2.0 open in 3.0 unchanged.

### New in 3.0

**Plugins of every kind**
- Hosts **VST3, VST (2.x), CLAP and LV2** instruments and effects (Project → Plugin Manager → Options → Scan). Every plugin is still tested in a separate process first, so a crashing plugin can't take the app down.
- Plugins open in their own windows with their real interfaces.
- **Automatic delay compensation**: plugins that add latency (look-ahead limiters, pitch correction, linear-phase EQs) stay in time with everything else.

**Mixing like a real console**
- **Aux buses and sends**: one shared reverb for the whole song. *+ Send* on any channel (pre- or post-fader), or *+ Track → Aux Bus*.
- **Output routing**: send several tracks into a group bus and control them with one fader.
- **Side-chain** for the compressor, gate and vocoder: duck the bass under the kick, gate a pad to the hi-hats.
- **Automate anything**: volume, pan, send levels and **every knob of every plugin** (right-click an automation lane).
- **Markers** (Intro, Verse, Chorus...) in the ruler: right-click to add, click to jump.
- **Song key** in the control bar: Scale Lock, Vocal Tune and the Loop library follow it.

**Time stretching**
- **Ctrl+drag** an audio clip's right edge to stretch it without changing the pitch.
- **Follow Song Tempo**: the clip's tempo is detected and it speeds up / slows down with the song. Stems from *Open in Studio* do this automatically, so you can slow a song down to practise.
- Transpose audio ±12 semitones without changing speed, **reverse**, **normalize**, half / double speed.

**Vocals**
- **Vocal Tune**: automatic pitch correction to the song's key, from natural to the hard robotic effect, with a live pitch display.
- **De-Esser** and **Vocoder** (robot voice, or make a synth sing with a side-chained vocal).

**HomeKeys 20: an 80s home keyboard, with full drum loops**
- 16 lo-fi tones (organs, toy strings, vibes, music box, choir...) through a *Vintage* knob for that warped-cassette sound.
- A built-in **rhythm box with 20 rhythms** (Slow Rock, Waltz, Bossa Nova, Rhumba, Beguine, Disco, 16 Beat, Ballad, Swing, Tango, Samba, Reggae...), fills, and **auto accompaniment** (single-finger or fingered chords play bass and chords for you). It follows the song tempo when the song plays, or runs on its own with START / SYNC START.
- **Vintage Rhythm Box** instrument and all 20 rhythms as editable drum loops in *Library → Drums*.
- Everything needed for slow, hazy, dream-pop songs: try *+ Track → HomeKeys 20*, the *Dream Pop Drift* loops, Tape Warble and the Shimmer bus.

**Arpeggiators and MIDI effects** (*+ MIDI FX* above any instrument)
- **Arpeggiator**: up, down, up/down, random, random walk, chord, converge / diverge; 1/1 to 1/32 with triplets and dotted notes; octaves, gate, swing, latch, rhythm patterns, **probability and ratchets** for the weird stuff.
- **Chord Trigger**, **Scale Lock**, **Note Echo**, **Randomizer**.

**Samplers and loopers**
- **Sampler**: drop in any sound and play it across the keyboard, as a one-shot, or **chopped into slices** (by transients or 4 / 8 / 16 / 32). Loop points with crossfade, envelopes, filter, glide, reverse.
- **Drum Pads**: 16 pads for your own samples (or four synthesized vintage kits), with tune, decay, filter, pan, reverse and choke groups.
- **Convert to Sampler Track**: slice any audio clip onto a Sampler track with a MIDI clip that plays it back, ready to remix.
- **Loop Station**: a looper pedal on any track. Record, overdub layer after layer, undo / redo, half speed, reverse, start on the bar, and put the loop on the track.

**Creative effects**: Shimmer Reverb, Tape Warble (wow, flutter, hiss, dropouts), Grain Cloud (granular), Beat Repeat (stutters and rolls), Pitch Shifter / Harmonizer, Auto Filter (synced wobbles, envelope wah), Flanger, Ring Modulator, Bitcrusher, Stereo Width. 36 built-in instruments and effects in total.

**More**
- **Loops library**: chord progressions, bass lines and arpeggios written in your song's key (pads, strummed, piano, organ, roots, disco octaves, walking bass, arps).
- **Bounce in Place**, **swing quantize**, **join MIDI clips**.

### Still here from 2.0
Sound Library (287 instruments, 13 drum kits), Studio Synth, Drummer (18 grooves), multitrack audio + MIDI recording with takes, piano roll, export to WAV / FLAC / OGG / MIDI, autosave. And **Play Along**: AI stem separation (drums, bass, guitar, keys, lead and backing vocals, other) and a full amp rig for your bass, guitar or mic.

Requirements: Windows 10/11 64-bit, a CPU with AVX2 (most PCs from 2015 on), 8 GB RAM recommended.
