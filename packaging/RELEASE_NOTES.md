## WOMANINSTEM 3.12.0 for Windows (64-bit): guitar and bass that sound like an amp in a room

**Download one of these:**
- `WOMANINSTEM-x.y.z-setup.exe`: installer with Start Menu / desktop shortcuts, "Open with WOMANINSTEM" for MP3/FLAC, and double-click to open Studio songs (`.wisproj`).
- `WOMANINSTEM-x.y.z-windows-x64.zip`: portable. Unzip anywhere and run `WOMANINSTEM.exe`.

Windows SmartScreen may warn because the app isn't code-signed: click **More info → Run anyway**.
Songs and rig presets from 2.0 to 3.11 open as before, with the new cabinets (old amp and cabinet choices are mapped to the closest models; the old *Room* knob is now the mic's *Distance*). The room mics, cable and new reverb spaces start switched off in old songs: pick a factory preset, or turn them up.

### New in 3.12

**Your guitar and bass through an amp in a room, not a DI.** Everything here is in the *Amp & Pedals* rig, so it works on the Studio's *Audio: Guitar or Bass* tracks and in Play Along.

- **New cabinets: the speaker and the mic, modelled.** The old cabinets were smooth EQ curves, which is a big part of why a DI'd guitar still sounded like a DI. Each of the 11 cabinets is now a physical model: the sound of every part of the cone as the microphone hears it from where it really is, averaged over the mic's diaphragm. Above about 1 kHz the cone stops moving as one piece: waves run out from the voice coil and bounce off the surround, the outer cone goes quiet, and the cone's break-up modes ring. That's where the jagged peaks and notches and the short metallic ring of a real speaker come from. The cab's other speakers (heard off axis), its box resonances and the floor are in there too, and each speaker is shaped to the published response of its family (Greenback-style and Vintage-30-style 4x12s, an open-back American 2x12, an alnico Blue 2x12, open-back and tweed 1x12s, and the classic bass cabs).
- **Mic placement that behaves like moving a real mic.** *Mic Pos* goes from the centre of the dust cap (bright, aggressive) through the cap's edge (the classic spot) to the edge of the cone (dark, woofy). The cabinet's old *Room* knob is now **Distance**: from touching the grille (lots of proximity bass) through 3 cm and 14 cm to 30 cm (where the other speakers and the floor join in) and 1 m. A ribbon up close is boomy and gets smoother as it moves back, as it does in a studio. Mics: dynamic, ribbon, condenser, dynamic + ribbon.
- **Room mics: the amp in a space.** A new *Room Mics* section puts the amp in a **Studio Live Room**, a **Small Room**, a **Big Live Room**, a **Concrete Garage** or on a **Hall Stage**, with a spaced pair of room mics a few metres back. The rooms are real-sized boxes: you hear the amp's direct sound arrive, the floor bounce, the walls and ceiling (bright straight ahead of the speaker, dull to the sides, only lows from behind a closed back) and a tail whose length and level follow the room's size. **Level** blends them with the close mic, in stereo, without changing the overall loudness.
- **Amps that react like amps.** Every amp model now has an **output transformer** whose core saturates on big low notes and pick thumps (early in the small vintage combos, late in the 100-watt heads), and a **speaker** that gives a little when you dig in. Together with the new cabinets, pick attacks lose the DI's clanky edge: on a real DI recording, the click of each pick attack measured 2 to 3 dB softer than in 3.11.
- **Guitar cable.** A new *Cable* knob in *Input*: a real lead adds capacitance across the pickup, which moves its resonant peak down and softens the brittle top you get from a short cable into an interface. 0 = as recorded; 6 m is a classic 20-foot lead.
- **Reverb spaces.** The rig's reverb has a *Type*: **Room**, **Plate**, **Spring** (the amp's own tank) and **Hall** are real-sounding spaces; **Classic** is the reverb from before.
- **All factory presets use them** (cable, mic placement, room mics and reverb space to suit each sound) and were **re-levelled on real recorded guitar**, so switching presets doesn't jump in volume and none of them clip. A new *Audio: Guitar or Bass* track starts on *Classic Rock Crunch* in a big live room.
- For a cabinet you know, loading its impulse response (`.wav`) still works, and so does a full-rig NAM capture.

### New in 3.11

**Daydream** (Studio: *+ Track → Daydream*, or Library → Sounds → Daydream)
- **A chord machine: one key, a whole chord.** Hold a chord button and play a key: **Maj**, **Min**, **Sus** or **Dim**, coloured with **6**, **m7**, **M7** and **9**. Hold more than one and they combine: *Maj + M7* is a major 7th, *Min + m7 + 9* a minor 9th, *Dim + m7* a half-diminished, *Sus + m7* a 7sus4. The screen names whatever you make (Cmaj9, Am7, F6/9, G7sus4, Bm7b5...).
- **On a MIDI keyboard** the eight buttons are the white keys of one octave (C2 to C3 by default: C Maj, D Min, E Sus, F Dim, G 6, A m7, B M7, C 9), so your left hand holds the chord and your right hand plays roots anywhere else. Press the buttons before or after the key, add a colour while the chord rings and it joins in. They're ordinary notes, so a part you record plays its chords back. On screen, click a button to latch it on. With no button held, keys play single notes.
- **Key Mode**: every key plays the chord its scale makes on it (in C major, D is Dm, G is G, B is Bdim); out-of-key notes snap to the scale, the colour buttons add the key's own 6th, 7th and 9th, and a chord button still overrides. Eight scales (major, minor, Dorian, Mixolydian, Lydian, Phrygian, harmonic and melodic minor), or **follow the song's key**. **Transpose** moves everything by semitones.
- **The voicing ladder**: each step up moves the chord's lowest note an octave up to the top, each step down moves the top note to the bottom, so one control walks a chord through all its inversions and up or down the keyboard; turn it while a chord rings and the notes move. The **bass ladder** walks the bass note through the chord's notes (C, C/E, C/G...). **Chord position** either follows the key you press or **stays in one place** (every chord is voiced around a centre note, so progressions move smoothly); **spread**: close, open (drop 2 or drop 3), or wide.
- **Performance modes** bring the chord to life: **Strum** (speed, up, down or alternating), **Strum 2 Octaves**, **Sloppy Strum** (human timing and touch), **Arpeggio** and **Arpeggio 2 Octaves** (1/4 to 1/32 with triplets, up, down, up and down, random, outside-in, gate, swing), **Pattern** (16 rhythm patterns that fit any chord: eighth pulse, offbeat stabs, broken chords, bossa comping, motorik chug, gallop, two hands, ballad picking, waltz...) and **Harp** (the chord swept up two to five octaves). **Lock** keeps your performance when you change the sound.
- **A separate bass synth** underneath: 8 sounds (*Deep Sub*, *Round Analogue*, *Warm Fingers*, *Rubber Band*, *Saw Growl*, *Hollow Square*, *Bell Bass*, *Tape Wobble*), playing with chords, with chords and single notes, or on its own; its own level, octave, glide and tone.
- **Three synth engines**, all editable on the *SOUND* page:
  - *Analogue*: two oscillators (saw, square, pulse, triangle, sine) with up to 4-voice unison, a sub oscillator, noise, four filters (24 and 12 dB low pass, band pass, high pass), filter and amp envelopes, an LFO for vibrato, filter, pulse width and level, and analogue drift.
  - *FM*: four operators in eight algorithms, each with its own ratio, level and envelope, feedback and touch.
  - *Reed Piano*: a modelled 60s reed electric piano, nothing sampled: the reed's own overtones struck by a felt hammer, the pickup's bark when you dig in, the key's thump and the damper.
- **Eight effects**: drive, tremolo (or auto-pan), phaser, flanger, chorus, ensemble, a tempo-synced delay (ping-pong) and reverb. The **FX** knob turns each sound's main effect; *Lock* keeps your effects when you change the sound.
- **Beats to play to**: 15 drum grooves (*Easy Pop*, *Laid-Back Hip-Hop*, *Four on the Floor*, *Motorik*, *Breakbeat*, *Bossa*, *Disco Strut*, *Slow Jam 12/8*, *Psych Shuffle*, *Lo-Fi Dust*, *One Drop*, *Afro Groove*, *Half-Time*, *Soft Brushes*, *Krautrock Toms*) on four drum machine kits, at the song's tempo or its own.
- **A looper** for sketching: free length (it starts with your first note) or 1 to 16 bars after a bar of count-in, then **overdub** layer on layer, **undo** the last one, or clear.
- **The screen** shows the chord name, its notes, a keyboard with the chord and the bass lit up, or a scope; click it to change the view.
- **45 sounds** in seven groups: *Keys* (Velvet Reed Piano, Barking Reed, Glass Tine Piano, Sunday Organ, Thrift Combo Organ...), *Pads* (Moon Pad, Velour Strings, Glacier, Deep Space Drift...), *Synths* (Psych Poly, Vintage Brass Synth, Lucid Chords, Rubber Funk Stabs...), *Plucks & Strums* (Twelve-String Strum, Nylon Strum, Harp Glissando, Kalimba Arp, Music Box...), *Bells*, *Arps & Patterns* (Night Drive Arp, Motorik Chug, Arp Rain...) and *Textures* (Cassette Memory, Underwater Organ, Hymn in a Cave...). Every sound is level-matched.
- Daydream is an original instrument with the same kind of feature set as hardware chord synthesizers; it isn't a copy of any of them (no names, artwork, layouts or sounds taken).

### New in 3.10

**Smart Drummer** (Studio: *+ Track → Smart Drummer*, or Library → Sounds → Smart Drummer)
- **A drummer that listens to your song and plays along.** Add it and it listens to everything else in the song: MIDI parts and audio (your recordings, loops, and the stems of a split song, muted ones too). It hears where the bass and the accents land, how busy and how loud each bar is, whether the song swings or shuffles, and where it changes: from your markers if you've set them (*Verse*, *Chorus*...), or found from the music (intro, verses, choruses, breaks, outro). Then it writes a part for it: quieter in the verses, harder in the choruses, the kick locking with the bass, a fill into every new section and a crash on its downbeat, resting in the breaks, and a final hit at the end.
- **It's your part to edit.** The drums land on the track as ordinary MIDI clips, one per section (*Verse 1 - Rock*, *Chorus - Rock*...): double-click one to change any hit in the piano roll. Playing it again replaces only the drummer's own clips; Ctrl+Z undoes it.
- **18 styles**: *Rock*, *Pop*, *Indie / Garage*, *Psych (laid back)*, *Punk*, *Metal*, *Funk*, *Soul / Motown*, *Hip-Hop (boom bap)*, *Lo-fi*, *Disco*, *House / Dance*, *Reggae (one drop)*, *Blues / Shuffle*, *Jazz (swing)*, *Bossa Nova*, *Folk / Brushes* and *Dream Pop*. Each has its own grooves at four strengths (quiet, verse, chorus, peak), its own fills (around the toms, snare builds, funk licks, shuffle triplets, jazz comping, a hip-hop pickup) and its own kit. The first time, it picks a style that suits what it heard (a shuffle gets the blues shuffle, a very fast song punk, a slow one dream pop).
- **How it plays**: *Complexity* (from just the time to ghost notes and syncopated kicks), *Energy*, *Follow the Band* (how tightly the kick sticks to the bass), *Fills*, *Swing* (taken from the song, or your own), *Humanize*, *Push / Lay Back*, *Normal / Half-time / Double-time*, *Hi-hat or Ride*, and the *Final Hit*. **Another Take** plays the same thing a different way. With *Rewrite as You Change It* on, the part follows the knobs as you turn them.
- **The song as the drummer heard it**: a strip of the sections over the loudness of every bar. Click a section to tell the drummer how to play it: *Quiet*, *Verse*, *Chorus*, *Peak* or *Rest*.
- **Build your own kit** (the *KIT* page): 17 pieces (kick, snare, side stick, clap, closed / pedal / open hi-hat, floor, low, mid and high toms, crash, ride, ride bell, tambourine, shaker, cowbell), each taken from any of **11 sampled kits** (Studio 1-3, Room, Power, Electronic, 808 / 909, Dance, Jazz, Brushes, Orchestral) or **4 drum machines**, with its own tune, decay, level and pan, and a mute. Click a piece's name to hear it. A kick from the Power Kit, a snare from the Jazz Kit and hats from a drum machine is fine. Then the whole kit's **room** (any of the 18 spaces), **punch**, **drive**, **tone** and **volume**. Every style brings a matching kit; turn off *Match the Kit to the Style* to keep yours, or *Reset the Kit to the Style* to get it back.
- The kit plays any drum part on the General MIDI drum map, so it works for your own drum tracks too.

### New in 3.9

**Choir Loft** (Studio: *+ Track → Choir Loft*, or Library → Sounds → Choir Loft)
- A choir, or a single singer, where **nothing is sampled**. Every singer is a modelled voice: their vocal folds make a glottal pulse (pressed or breathy, with the tiny cycle-to-cycle wobbles real folds have, and a rasp if they're gravelly), then their throat, mouth, lips and nose shape it into vowels. No two notes come out quite the same, and a choir of them sounds like people, not copies.
- **Singers built from people**, up to 64. Set the cast: how many, women and men, the average age and the spread, how different everyone is; **New Cast** re-rolls them. Click anyone on the stage to see their face and change them:
  - *Body*: woman to man, age (6 to 90), head size, face length, jaw width, chin, cheekbones, nose, lip fullness, mouth width, neck length, chest. These change the voice the way they do in life: a longer face and neck make a longer vocal tract and darker vowels, a wider jaw opens the vowels up, fuller lips round them, a bigger nose makes them more nasal. Children have short tracts and high voices, voices settle after puberty, older singers wobble more, darken and get breathier.
  - *Voice*: where their range sits (alto or soprano, bass or tenor), rasp, air, vibrato speed and width, brightness, and how well they hit the note.
  - Each singer gets a name, a voice type (Soprano, Mezzo, Alto, Tenor, Baritone, Bass, Treble), a range and a break; the **vocal tract** view shows their tube, its length and the formants of the vowel they're singing. **LOCK** keeps a singer when you re-roll; **NEW FACE** swaps one person.
- **Ten singing styles**: *Classical* (a ringing core, F1 tuned to high notes, wide even vibrato), *Choral*, *Early Music* (straight tone), *Gospel* (pressed, scooping), *Pop* (close and breathy), *Folk*, *Belt*, *Blues & Raspy*, *Lullaby* (half-whispered) and *Overtone* (throat singing: the whistle above the drone follows the vowel).
- **Four ways to share the notes**: *Sections* (the whole choir shares the chord you hold, each singer taking the note that suits their voice), *Octaves* (each in their own range: one key, a whole choir), *Round Robin* (each new note goes to the next singer or few), *Solo* (the selected singer, legato: overlap notes and they glide).
- **Vowels**: drag the dot on the vowel chart (*ee* to *oo*, *ah* to *aw*), round or spread the lips, or **hum** with the mouth closed. **Vowel motion**: a wave between two vowels, a move from one to the other on every note (*ooh* opening to *aah*), steps, a random vowel per note (a choir that seems to sing words), or a slow drift. Each singer can move on their own or the choir together.
- **Onsets**: soft, hard, *h*, *m*, *n*, *l*, *d* (doo, dah), *b*, *w*, *y*, or random, for doo-wop and backing parts.
- **Expression**: dynamics (soft notes get breathier and darker, loud ones pressed and bright), breath, vibrato, **ensemble looseness** (late entries, pitch scatter, drifting vibratos), rasp, **breaths between phrases**, attack, release, glide, fine tune. The mod wheel deepens the vibrato, CC11 is expression, the sustain pedal holds, MPE bends and presses each note.
- **The stage**: the singers stand in rows, an arc, sections (high voices left, low voices right), a line or a cluster, and you can drag anyone somewhere else. In front of them is a modelled stereo pair (*spaced omnis*, *near-coincident*, *X-Y cardioids*, *crossed figure-8s* or *one mic*): every singer reaches each microphone at their own distance, angle and level, so the image is real. **Mic distance** trades presence for room; the **room** is any of the 18 designed spaces (or none) (church, cathedral, concert hall, wooden studio, cave, forest, canyon...).
- **31 presets**: *Cathedral Choir (Aah)*, *Chamber Choir (Ooh)*, *Mixed Choir (Oh)*, *Boys' Choir (Treble)*, *Women's Choir*, *Men's Chorus*, *Monks (low drone)*, *Hum Choir (Mmm)*, *Gospel Choir*, *Folk Harmony Trio*, *Barbershop Quartet*, *Doo-Wop Backing (Doo)*, *Pop Backing (Ooh)*, *Wordless Film Choir*, *Vowel Sweep Pad*, *Talking Choir*, *Ghost Choir*, *Village Choir (loose)*, *Old Folks' Choir*, *Classroom Singalong*, *Stadium Crowd (64)*, *Early Music Consort*, *Soprano Solo*, *Tenor Solo*, *Lullaby Voice*, *Blues Holler*, *Bass Drone*, *Overtone Singer*, *Belting Ensemble*, *Dry Studio Choir* and *Init*.
- Choir Loft is an original instrument with the same kind of feature set as some commercial "built from people" choir plugins; it isn't a copy of any of them (no names, artwork, layouts or presets taken).

**Chord Keys** (Studio: the **Chords** button, or `H`; `Ctrl+Shift+K` turns them on)
- Play **whole chords from your computer keyboard** into any instrument track: the number row and the three letter rows are 40 chord keys.
- **You choose what's on every key**: click a key and pick the root, the chord type (major, minor, diminished, augmented, sus2 / sus4, power chords, 7, maj7, m7, m(maj7), m7b5, dim7, 7sus4, 6, m6, add9, 6/9, 9, maj9, m9, 11, m11, 13, maj7#11, 7b9, 7#9, single notes, octaves...), the inversion, the octave, the voicing (close, open, spread, wide) and a bass note for slash chords. Or just type the name: `Am7`, `F#m7b5`, `C/G`, `Bbmaj9`.
- **Move them around**: drag a key onto another to swap the chords, Alt-drag to copy. Right-click to copy, paste or clear; *Fill Row* puts the chords of the key on a row (triads, sevenths, or sus / add9 colours).
- **Ten banks to start from**: *Pop & Rock (C major)*, *Minor Moods (A minor)*, *Jazz & Soul*, *Blues (A)*, *Neo-Soul & Lo-fi*, *Psychedelic 60s*, *Dream Pop*, *Folk & Open Chords (G)*, *Every Major & Minor* and an empty one. Make your own (new, duplicate, rename, delete), **save banks to files** and load them back; they're remembered between sessions.
- **Follows the song's key**: banks are written in a home key and move to your song's key (a C major bank in a D major song plays D, Em, F#m...; major banks fit minor songs through the relative key).
- **Smooth voice leading** picks the inversion closest to the last chord, like a keyboard player's hands. Also **root in the bass**, **strum** (0 to 120 ms, up, down or alternating), **latch** (chords ring until the next one), velocity, `[` `]` octave and `-` `=` transpose while you play.
- The keyboard on screen lights the chords as you play them, and the status bar shows the chord and its notes. Chords record into MIDI clips like anything else you play.

**Fixes**
- Junkyard Percussion and the Whistler no longer click when they're pushed very loud (their output ceiling had a small jump in it).

### New in 3.8

**Echo Deck** (Studio: any insert slot → *Space → Echo Deck*, or Library → Effects; on the Play Along pedalboard: *+ Add pedal → Echo Deck*)
- A full analog-style echo processor with its own front panel: tape reels that spin at the tape speed, a VU meter (input or echo), amber readouts, illuminated keys and big chicken-head knobs. Resize the window and it scales.
- **10 echo machines** (the *Style* window). Each repeat goes round the machine again, so it darkens, saturates and wobbles the way the real thing does:
  - *Studio Tape* (clean pro reel-to-reel), *Tape Echo Unit* (looped tape box: dark, fluttery, a little spring), *Valve Tape* (thick valve-preamp drive), *Worn Tape*, *Cassette*,
  - *Bucket Brigade* (analog chip: the longer the echo, the slower its clock and the darker and grittier the repeats),
  - *Oil Can* (smeared and swimming), *Early Digital* (80s 12-bit rack), *Clean Digital* (transparent), *Lo-Fi Radio*.
- **Echo time** in milliseconds or as a note: **1/64 to 2 bars**, straight, **dotted** or **triplet**, following the **song's tempo** or a **tapped tempo** (the **TAP** button: in MS mode a tap sets the time itself). Turn the time while it plays and the pitch glides like a tape machine.
- **Modes**: **Single** (true stereo), **Dual** (a second echo time on the right, from x1/4 to x2), **Ping-Pong**, and **Rhythm**: an 8-step multi-tap pattern you draw, from 1 to 8 steps long, repeating with the feedback.
- **Feedback up to 110%**: past 100% it runs away into a saturated, held wash instead of exploding. **Hold** freezes what's in the loop while you play on top.
- **Low cut / high cut** inside the loop, **Saturation**, **Input** (drives the machine) and **Output**.
- **Tweak**: **Groove** (pushes or swings every other repeat), **Feel** (echoes ahead of or behind the beat), **Wobble** (wow and flutter, 0 to 2x the machine's own), **Diffusion** (smears repeats towards a reverb), **Width**, and **Duck** (echoes duck while you play, swell back in the gaps).
- **28 presets**: *Studio Tape: Dotted 1/8*, *Slapback (50s)*, *Vocal Throw 1/4*, *Tape Echo Wash*, *Dub Runaway*, *Worn Tape Lullaby*, *Cassette Dream*, *Bucket Brigade Dark*, *Bucket Brigade Doubler*, *Oil Can Swirl*, *Ping-Pong 1/8*, *Dual: 3 Against 4*, *Double Tracker (ADT)*, *Rhythm: 3-3-2*, *Rhythm: Gallop*, *Swing Eighths*, *Early Digital Lead*, *Radio Echo*, *Psych Guitar Wash*, *Bass Echo (tight lows)*, *Ducked Vocal Echo*, *Lazy Behind-the-Beat*, *Short Tape Flange*, *Ambient Tape Smear* and more.
- **New Tape Echo Bus** in the mixer's *+ Send* menu: an Echo Deck on a return bus, 100% wet.
- In Play Along, where there's no song tempo, tap the tempo in (or use MS mode).

### New in 3.7

**Junkyard Percussion** (Studio: *+ Track → Junkyard Percussion*, or Library → Sounds → Junkyard Percussion)
- A new instrument for clanging metal, knocking wood, scraped strings and atmospheres, in the spirit of *Swordfishtrombones* and *Bone Machine*. Nothing is sampled: every sound is built from resonating metal and wood, plucked and bowed strings and filtered noise, so no two hits are quite the same (**Humanize**).
- **The Junk Kit** puts a different thing on every key, from C2 up:
  - *body percussion*: finger snaps (tight, fat, bright, soft), finger clicks, hand claps, a room of claps, knee and thigh slaps, a tongue click, a chest thump, a foot stomp on floorboards,
  - *wood*: a crate, a chair knock, a plank, low and high log drums, a wood block, bones, a door knock, a cajon-like box, a cello struck *col legno*, a slap on the double bass's body, a double bass pizzicato,
  - *metal*: brake drums (open and hand-muted), a scaffold pipe, an anvil, a bell plate, a tin can, a pot lid, an oil drum, a jail-door clang, a chain rattle, a spring boing, a glass bottle,
  - *sound-space textures* that keep going while you hold the key: bowed saw, bowed metal, bowed double bass and cello, wind, a distant rumble, rain on a tin roof, creaking wood, cave drips, mains hum, a thunder sheet, a waterphone and a deep gong.
- Or play **one source across the keyboard**, in tune: marimba, bass marimba, brake drums, pipes, bell plates, glass bottles, log drums, wood blocks, double bass and cello pizzicato, col legno, bowed double bass, bowed cello, bowed saw, bowed metal, waterphone, wind (low keys moan, high keys whistle), deep gongs, oil drums, tin cans, pot lids.
- Knobs: Tune, Decay, Tone, Humanize, **Loose Junk Rattle**, Width, Grit, and a **Space** to put it all in.
- Four new spaces (also in Piano Room): **Concrete Storeroom**, **Tin Shed**, **Cave** and **Stairwell**.
- 24 presets: *Junkyard (concrete storeroom)*, *Swordfish Parade*, *Snaps & Claps*, *Tin Shed Clatter*, *Cave Sound Space*, *Night Forest Space*, *Bowed Saw (ghostly)*, *Bowed Metal (horror)*, *Waterphone Drift*, *Double Bass Pizzicato*, *Dusty Marimba* and more.

**Finger-snap loops, junk grooves and sound spaces** (Library → Drums)
- **Finger Snaps & Body Percussion**: snaps on 2 & 4, on every beat, doo-wop, swung jazz-club snaps, a finger-click shuffle, busy 16ths, half-time, snaps & knee slaps, the hand jive, gospel stomp & clap, tongue clicks & snaps.
- **Junkyard Grooves**: *Bone Machine Stomp*, *Swordfish March*, *Junkyard Shuffle*, *Clanking Machinery*, *Funeral Procession*, *Kitchen Sink Polka*, *Rattle & Bones*, *Col Legno Pulse*.
- **Sound Spaces**: 8-bar atmospheres for games and film: *Haunted Basement*, *Storm Shelter*, *Abandoned Factory*, *Night Forest*, *Underwater Cave*, *Ghost Ship*, *Bone Machine Room*. Each lands on its own track in its own room; loop the clip for as long as the level lasts, and export it as WAV.
- They're MIDI, so **change the song's tempo and they follow**: no stretching, no artefacts. Double-click one to drop 8 bars at the playhead (it makes a Junkyard track if you don't have one).

**Whistler** (Studio: *+ Track → Whistler*)
- Someone whistling. Play **legato** (hold one key while you press the next) and it **glides** from note to note; it keeps sliding on from the last note after a short gap too, and **scoops up** into the first note of every phrase. Breath, air, a vibrato that comes in after a moment, a human wobble, a fall-off at the end, echo and reverb. Pitch bend bends it; the mod wheel adds vibrato.
- Presets: *Lonesome Whistle*, *Happy-Go-Lucky*, *Close & Breathy*, *Two-Finger Whistle*, *Old Man on the Porch*, *Ghost Whistle*, *Bird Trills*, *Slide Whistle*, *Dry*.

**Psych-pop bass** (Play Along rig and *Amp & Pedals*)
- New amp: **Bass: Psych Pop (rubbery DI)**: a 60s hollow-body bass DI'd into a hot valve preamp that compresses itself, low end kept tight, the mids pushed into a round honk, the fizz rolled off.
- Two new presets (approximations, not endorsements): **Bass - Is It True** (round, bouncy, squashed flat, mostly DI) and **Bass - Feels Like We Only Go Backwards** (woollier: a warm fuzz, compressed harder, darker, a slow phaser, saturated tape). Best with a short-scale or violin-style bass and flatwounds, played with the fingers near the neck.

**Vocal sounds for your mic**
- *+ Track → Audio: Microphone / Vocals* now asks which sound you want; right-click any audio track → **Vocal Sound** to change it later, or double-click one in Library → Effects → *Vocal Chains*. 25 chains, all made from the Studio's own effects, so every knob stays editable:
  - *Studio & Pop*: Clean Studio Vocal (now the default), Warm Radio Voice, Hard-Tune Pop, Soul Ballad, Garage Rock Shout,
  - *Folk & Roots*: Folk Room (live take), Old Folk Record (60s), Laurel Canyon Double, Campfire, Back-Porch Blues,
  - *Psychedelic*: Psych ADT (60s double tracking), Rotating Speaker Voice, Phaser Haze, Shoegaze Wash, Space Echo Dub, Lo-Fi Bedroom Tape, Cosmic Flanged Choir,
  - *Weird & Extreme*: Telephone / Megaphone, Haunted Whisper, Demon Voice, Helium Chipmunk, Robot (vocoder), Ring-Mod Alien, Extreme Metal Scream, Cathedral Ambience.
- Play Along's rig gets ten of them as presets too (*Vocal - Clean Studio*, *Vocal - Psych ADT Double*, *Vocal - Demon*...), for singing along with a song.

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
