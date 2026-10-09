## WOMANINSTEM 3.6.1 for Windows (64-bit): the dream organ and its drum machine, plugins in the bottom panel, old 32-bit plugins (Delay Lama!)

**Download one of these:**
- `WOMANINSTEM-x.y.z-setup.exe`: installer with Start Menu / desktop shortcuts, "Open with WOMANINSTEM" for MP3/FLAC, and double-click to open Studio songs (`.wisproj`).
- `WOMANINSTEM-x.y.z-windows-x64.zip`: portable. Unzip anywhere and run `WOMANINSTEM.exe`.

Windows SmartScreen may warn because the app isn't code-signed: click **More info → Run anyway**.
Songs and rig presets from 2.0 to 3.5 open unchanged (old amp and cabinet choices are mapped to the closest new models).

### New in 3.6.1

**HomeKeys 20: the '81 portable keyboard's own rhythm section**
- New **Rhythm Bank: Portable '81**, laid out like the rhythm section of the early-80s portable keyboard the dream organ comes from: its **8 rhythms (March, Disco, Waltz, Rock, Tango, Swing, Rhumba, Samba)**, each with **Variation I / II**, and the **8-Bar Variation** switch that drops in a fill every eighth bar.
- New **Rhythm Sound: Portable '81**: that keyboard's simple analogue drums (a soft, boomy bass drum, a thin hissy snare, hi-hats and cymbal made of noise, nearly mono).
- New presets: **Dream Organ + '81 Rock Beat**, **Dream Organ + '81 Waltz**, **Dream Organ + '81 Rhumba** (one-finger chords in the left hand). *Teen Dream Organ* and *Gila Organ* now start the '81 Rock rhythm when you press START.
- The keyboard's exact factory patterns aren't documented anywhere, so these are written in its style rather than copied note for note. *Thrift Store Organ + Slow Rock* keeps the old organ rhythm unit's slow rock (the '81 portable doesn't have one).

### New in 3.6

**HomeKeys 20: the dream organ** (Studio: *+ Track → HomeKeys 20*, or Library → HomeKeys 20)
- Two new tones, **Dream Organ 1** and **Dream Organ 2**, modelled on the cheap early-80s digital home-keyboard organs that Beach House built *Teen Dream*, *Devotion* and their first album on: square-wave organ stops stored as coarse, stepped digital waveforms, smoothed by a warm analogue filter. Organ 1 is the hollow, reedy one; Organ 2 adds the percussive "bite" at the start of each note.
- New effects on the keyboard: **Amp Drive** (a small valve amp), **Wobble** (a vibrato pedal, with speed) and a big, soft **Reverb** (with size).
- New presets, first in the list: **Teen Dream Organ**, **Gila Organ (Devotion)**, **Thrift Store Organ + Slow Rock ('06)** (the keyboard's own drum machine and one-finger chords, like the first album) and **Dream Organ, Dry** (no effects, add your own).
- A new HomeKeys track now starts on *Teen Dream Organ*. Your saved songs keep their sounds.

**Plugins open in the bottom panel**
- Instrument and effect controls (Amp & Pedals, HomeKeys, Piano Room, Beat Lab and every built-in effect) now open **under the song**, next to Editor / Mixer / Keys, instead of in a window floating over it. The panel grows to fit.
- Open several and they sit side by side as tabs. Click **Plugin** at the top right (or press **P**) to show or hide it.
- **Pop out** puts one in its own window (and later ones follow, until you dock again); **Dock in the Studio** at the top of that window brings it back.
- Plugins from other makers (VST, VST3, CLAP, 32-bit) still open in their own windows.

### New in 3.5

**Old 32-bit plugins work** (Delay Lama and the other classic freeware VSTs from the 2000s)
- WOMANINSTEM is a 64-bit app, and Windows can't load a 32-bit plugin inside a 64-bit program. 3.5 includes a **32-bit bridge** (`wisbridge32.exe`): the old plugin runs in its own little process and its sound, MIDI, tempo, parameters and saved settings are passed back and forth. If an old plugin crashes, only the bridge goes down, never your song.
- **Project → Add a Plugin File...** (or **+ Track → Plugin from a File...**): pick the `.dll`. WOMANINSTEM keeps its own copy (in `%APPDATA%\WOMANINSTEM\Plugins`, so it still works if you tidy up your Downloads), tests it in a separate process and puts it on a new track with its window open. From then on it's in *Library → Sounds → Plugin Instruments* like any other plugin. Works for `.vst3` and `.clap` files too.
- **Your plugins folder**: anything in `%APPDATA%\WOMANINSTEM\Plugins` (or a `Plugins` folder next to `WOMANINSTEM.exe`) is picked up automatically when the app starts. *Project → Open My Plugins Folder* opens it.
- A 32-bit plugin's own window (Delay Lama's singing monk included) opens as a separate window next to WOMANINSTEM. Close it and click *Show its window* to bring it back.
- Yodel Yeti (3.4) is gone: Delay Lama itself now works.
- **Plugin instruments in Play Along**: anything you've added in the Studio (VST3, VST, 32-bit VSTs, CLAP) can now be played from **KEYS** along with a song.

### Fixed in 3.4.1
- **Piano Room is now in the Studio**: *+ Track → Piano Room* (its window opens straight away), the track menu's *Instrument* list, and the Library's *Sounds* tab. In Play Along it's under **KEYS** (Ctrl+K).

### New in 3.4

**Piano Room: real pianos in real (and unreal) places** (Studio: *+ Track → Software Instrument*, Library → Piano Room; Play Along: **KEYS**)
- Three sampled pianos: a **Concert Grand** (Yamaha C5, 6 velocity layers), a darker **Vintage Grand** (Steinway) and a **Upright** (Kawai, recorded in a living room), plus a **Modelled Grand** built from physics and a **Toy Piano**.
- **Character**: hammers (soft to hard), felt, lid, dynamics, damper release, stretch tuning, A4 tuning (415 to 466 Hz), **honky-tonk** detune, **tacks**, **age** (every key its own amount out of tune, unisons drifting, wobble), key and pedal noise, sympathetic string resonance, latched sustain. The top keys ring on like a real piano's (no dampers).
- **Tone**: bass / body / treble, four mic positions (inside the lid to the back of the room), width, drive, compressor, tape (wow, flutter, saturation, hiss) and lo-fi (old radio / megaphone).
- **Space**: put the piano in a vocal booth, living room, wooden studio, big live room, bar, bathroom, concert hall, church, cathedral, **forest clearing**, **canyon** (echoes off the far walls), underground car park, plate or spring tank. Amount, size, distance, tone and pre-delay. The editor shows the piano in the place you picked.
- 21 presets, including *Intimate Ballad Grand* and *Murder Ballad Grand* (Nick Cave territory), *Rain Dog Upright* and *Junkyard Parlour* (Tom Waits-style), *Bohemian Rock Grand* and *Stadium Rock Grand* (Queen-style), *Swedish Psych Upright* and *Forest Cabin Psych* (Dungen-style), *Honky-Tonk Saloon*, *Tack Piano*, *Felt Piano*, *Lo-Fi Cassette Keys*.

**KEYS in Play Along** (the **KEYS** button on the rig, or Ctrl+K)
- Play a piano (or the Sound Library, a synth, HomeKeys, or plugin instruments such as Delay Lama) along with any song, from a **MIDI keyboard** (plug it in, it just works) or your **computer keys** (A W S E D F T G Y H U J K..., Z / X change octave). Sustain button, volume, every knob one click away. The keys are recorded with you and drawn by the Scope.

**Artist-style rig presets** (Play Along rig and *Amp & Pedals*; approximations, not endorsements)
- *Psych Phaser Fuzz* and *Psych Pop Hollow-Body* bass (Tame Impala-ish), *Garage Fuzz Blowout* (Ty Segall-ish), *Heavy 70s Fuzz Riffs* (FUZZ-ish), *60s Jazz Box* (the Julie London sessions), *12-String Jangle* (George) and *Casino Crunch* (John), *Doom Sludge* guitar and bass (Hell, *HEVY*). Several use the new pedalboard (phaser, treble booster, octave-up, sub octave, a 12-string octave voice).

### New in 3.3

**Pedalboard: build your own chain** (the row of pedals at the top of *YOUR RIG*, and in the Studio's *Amp & Pedals*)
- Your whole signal chain is shown left to right: gate, compressor, drive, **amp + cab**, EQ, tape, chorus, delay, reverb. **Drag any of them to re-order**: put the delay before the drive for distorted echoes, the reverb before the amp for a surf drip, the compressor after the drive... Pedals left of the amp go into its input; pedals to the right sit in its effects loop.
- **+ Add pedal** puts up to 16 extra pedals on the board, as many of each as you like. New stomp boxes: **Drive Pedal** (overdrive, distortion, fuzz, bass drive), **Boost** (clean, treble booster, mid push), **Wah** (rock it with the Pedal knob, let your picking move it like an envelope filter, or sweep it with an LFO; guitar and bass ranges), **Octaver** (one and two octaves down, one up) and **Volume Swell**. Plus every Studio effect: phaser, flanger, chorus, tremolo, ring modulator, auto filter, pitch shifter, shimmer reverb, tape warble, bitcrusher, grain cloud, loop station, saturator, compressor, EQ and more.
- Click a pedal to open its knobs and presets, click its light to switch it on or off, right-click to remove, duplicate or move it.
- The pedalboard is saved in your rig presets and with each song. Presets from before 3.3 load with the standard order.

### New in 3.2

**Scope: a light show drawn by your playing** (the **Scope** button at the top, or **Ctrl+Shift+O**)
- A glowing oscilloscope beam, like the psychedelic projections behind bands such as Tame Impala, that moves with the sound in real time.
- Watch **your instrument**, **the song**, **everything**, or **you vs the song** on the two axes (Play Along). In the Studio: the **master**, the **selected track** (even while you play it live), or **track vs master**.
- Three shapes: **Swirl** (a note draws a circle that grows the harder you play; chords and distortion turn it into flowers, knots and scribbles), **XY** (classic Lissajous figures, left against right) and **Wave** (the waveform held still).
- Six colour schemes: *Tame (lime + aqua)*, *Phosphor green*, *Aqua*, *Hot pink*, *Amber*, *Rainbow*. Knobs for size, trail (how long the beam glows), glow, spin, tangle and brightness; *Mirror* turns it into a kaleidoscope; *Auto size* keeps it filling the screen whether you play softly or hard.
- It's a separate window: drag it onto a **projector or second screen** and press **F** (or double-click) for **full screen**. The controls and mouse pointer hide themselves after a moment; Esc comes back. Keys: 1 / 2 / 3 shapes, C colours, M mirror.
- It reads the audio before your monitor volume, so it still reacts if you monitor directly through your interface.

### New in 3.1

**Beat Lab: a groovebox for beats and loops** (*+ Track → Beat Lab*, or *Library → Sounds → Beat Lab*)
- 8 lanes: four vintage drum machines, a **Glitch Lab** kit (clicks, zaps, FM blips, bit-crushed hits, sub booms), or **drag your own samples and loops onto a lane**. Loops are chopped across the lane's steps so they **stay in time at any tempo**; move steps around to re-arrange the beat. Or repitch them like a record.
- Steps with velocity, **rolls / ratchets** (2 to 16 hits, even, pitch-rising, pitch-falling or fading), **chance**, pitch, micro-timing and reverse. Euclidean rhythms, randomise (tame to wild), shift, copy.
- 8 patterns (switch on the bar, or chain them), swing, **per-lane length and rate** for polymeters (5 against 7 against 16...), up to 64 steps.
- Per lane: volume, pan, tune, decay, DJ filter with resonance, drive, bit-crush, reverb and delay sends, choke groups.
- **Perform**: hold Stutter (1/4 to 1/64), Tape Stop, Reverse or Fill; sweep the XY filter pad.
- **Glitch / IDM section**: *Mutate* gives every bar its own variation, *Chaos* throws in rolls, reversals and pitch jumps, *Break Shuffle* re-orders loop slices for drill'n'bass edits. Presets: Drill Machine, Polymeter Maze, Ambient Glitch, Braindance.
- 14 presets: Boom Bap, Lo-Fi Hip Hop, House, Techno, Trap, Breakbeat, Drum & Bass, Dream Pop Machine, Reggaeton, Afrobeat and the four IDM kits.
- **Pattern to Song** turns the pattern into a MIDI clip at the playhead (sliced loops still play the right slices).

**Amps 2.0** (Play Along rig and the *Amp & Pedals* plugin)
- **12 amp models** with modelled tone-stack circuits, preamp, power amp, sag and bloom. Guitar: American Clean, Tweed Breakup, British Chime, British Crunch, British Lead, Modern High Gain, Smooth Overdrive. Bass: 60s British Valve, Classic Tube 8x10, Vintage Flip-Top, Modern Growl, Studio DI.
- **11 cabinets** with **microphone choice** (dynamic, ribbon, condenser, dynamic + ribbon), **mic position**, **room** and a phase-aligned **DI blend**. Zero-latency convolution.
- **Strings & Pickups**: make any bass sound like flatwounds, a 60s violin bass or a foam-muted bass, or brighter roundwounds; guitar single coils or humbuckers. One knob, no EQ.
- **Bass Drive** that keeps your clean low end, and **Tape** saturation.
- **22 loudness-matched presets** that sound finished straight away. For bass: *60s Merseybeat (violin bass)*, *Late 60s Studio (DI + amp)*, *Motown Flatwound*, *Classic Rock 8x10*, *Modern Growl*, *Modern Clean Hi-Fi*, *Punk Pick*, *Dub Deep*, *Fuzz Bass*, *Clean DI*.

**Repeat bar fixed**
- Studio: the yellow repeat (cycle) bar now has its own strip at the top of the ruler. Drag to draw it, drag its edges to resize, drag the middle to move, click to turn it on / off, double-click a bar to repeat just that bar, right-click for quick choices (4 bars, selected clips, between markers, double, halve, move).
- Play Along: drag the edges of a loop to adjust it.

### Also new in 3.0

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
