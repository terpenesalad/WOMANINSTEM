#include "Junkyard.h"
#include "RoomIr.h"
#include <cmath>
#include <mutex>

namespace wis::daw
{

using namespace prm;

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float mtof (float note) noexcept { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

    // ---- resonance sets: (frequency ratio, relative amplitude) -------------------------------------------
    enum class Modes { none, bar, marimba, brake, anvil, plate, can, lid, drum, wood, hollow, glass, gong, single, dense, spring };

    struct ModeSet { std::vector<std::pair<float, float>> m; };

    const ModeSet& modeSet (Modes s)
    {
        static const ModeSet none {}, single { { { 1.0f, 1.0f } } };
        static const ModeSet bar     { { { 1.0f, 1.0f }, { 2.756f, 0.5f }, { 5.404f, 0.25f }, { 8.933f, 0.12f }, { 13.34f, 0.06f } } };   // free-free bar / pipe
        static const ModeSet marimba { { { 1.0f, 1.0f }, { 3.93f, 0.3f }, { 9.2f, 0.1f } } };                                                // tuned, undercut bar
        static const ModeSet brake   { { { 1.0f, 1.0f }, { 1.47f, 0.8f }, { 2.09f, 0.7f }, { 2.56f, 0.6f }, { 3.12f, 0.55f }, { 3.83f, 0.45f },
                                         { 4.41f, 0.4f }, { 5.26f, 0.3f }, { 6.18f, 0.25f }, { 7.3f, 0.18f }, { 8.6f, 0.12f }, { 10.1f, 0.08f } } };
        static const ModeSet anvil   { { { 1.0f, 1.0f }, { 2.31f, 0.7f }, { 3.9f, 0.6f }, { 5.42f, 0.45f }, { 7.1f, 0.3f }, { 9.3f, 0.2f } } };
        static const ModeSet plate   { { { 1.0f, 0.8f }, { 1.59f, 1.0f }, { 2.14f, 0.7f }, { 2.30f, 0.7f }, { 2.65f, 0.6f }, { 2.92f, 0.5f },
                                         { 3.16f, 0.45f }, { 3.50f, 0.4f }, { 4.06f, 0.3f }, { 4.6f, 0.25f }, { 5.4f, 0.18f }, { 6.3f, 0.12f } } };
        static const ModeSet can     { { { 1.0f, 1.0f }, { 1.73f, 0.7f }, { 2.2f, 0.6f }, { 3.1f, 0.5f }, { 3.9f, 0.35f }, { 5.3f, 0.2f } } };
        static const ModeSet lid     { { { 1.0f, 0.6f }, { 1.33f, 0.8f }, { 1.79f, 1.0f }, { 2.48f, 0.7f }, { 3.05f, 0.6f }, { 3.77f, 0.5f },
                                         { 4.6f, 0.4f }, { 5.5f, 0.3f }, { 6.71f, 0.25f }, { 8.0f, 0.2f }, { 9.6f, 0.15f }, { 11.3f, 0.1f } } };
        static const ModeSet drum    { { { 1.0f, 1.0f }, { 1.59f, 0.8f }, { 2.14f, 0.6f }, { 2.65f, 0.5f }, { 3.16f, 0.45f }, { 3.65f, 0.3f },
                                         { 4.15f, 0.25f }, { 4.65f, 0.2f } } };
        static const ModeSet wood    { { { 1.0f, 1.0f }, { 2.57f, 0.45f }, { 4.3f, 0.2f } } };
        static const ModeSet hollow  { { { 1.0f, 1.0f }, { 1.8f, 0.6f }, { 2.9f, 0.4f }, { 4.4f, 0.25f }, { 6.1f, 0.12f } } };
        static const ModeSet glass   { { { 1.0f, 1.0f }, { 2.32f, 0.5f }, { 4.25f, 0.3f }, { 6.6f, 0.15f } } };
        static const ModeSet gong    { { { 1.0f, 1.0f }, { 1.48f, 0.7f }, { 1.99f, 0.8f }, { 2.44f, 0.6f }, { 2.9f, 0.5f }, { 3.6f, 0.45f },
                                         { 4.27f, 0.35f }, { 5.1f, 0.25f }, { 6.0f, 0.2f }, { 7.2f, 0.15f } } };
        static const ModeSet spring  { { { 1.0f, 1.0f }, { 2.02f, 0.5f }, { 3.05f, 0.3f }, { 4.1f, 0.15f } } };
        static const ModeSet dense = []
        {
            // a tangle of metal (a sheet, a frame hung with junk): sixteen crowded, unrelated resonances
            ModeSet d;
            juce::Random r (1992);
            float f = 1.0f;
            for (int i = 0; i < 16; ++i)
            {
                d.m.push_back ({ f, 1.0f / (1.0f + 0.12f * (float) i) });
                f *= 1.12f + 0.22f * r.nextFloat();
            }
            return d;
        }();
        switch (s)
        {
            case Modes::bar: return bar;         case Modes::marimba: return marimba; case Modes::brake: return brake;
            case Modes::anvil: return anvil;     case Modes::plate: return plate;     case Modes::can: return can;
            case Modes::lid: return lid;         case Modes::drum: return drum;       case Modes::wood: return wood;
            case Modes::hollow: return hollow;   case Modes::glass: return glass;     case Modes::gong: return gong;
            case Modes::single: return single;   case Modes::dense: return dense;     case Modes::spring: return spring;
            case Modes::none: default: return none;
        }
    }

    // ---- a sound: a recipe of resonators, exciters, strings and noise ------------------------------------
    struct SoundDef
    {
        const char* name = "";
        float hz = 440.0f;            // reference pitch (what the kit key plays); every other frequency scales with it
        bool sustain = false;         // keeps going while the key is held
        float attack = 0.005f;        // seconds: the sustained parts' fade in
        float release = 0.4f;         // seconds: after the key comes up
        // modal resonators
        Modes modes = Modes::none;
        float t60 = 0.5f;             // seconds, lowest mode
        float damp = 0.5f;            // higher modes die faster: t60 * ratio^-damp
        float modeLevel = 1.0f;
        float spread = 0.004f;        // random detune of the modes per hit (beating metal)
        // strike: a half-sine contact pulse (shorter = harder mallet)
        float strike = 1.0f, contactMs = 1.0f;
        int hits = 1;                 // claps, bones and knocks are several hits
        float hitGapMs = 0.0f, hitJitter = 0.0f;
        // a band of noise straight to the output (snaps, claps, breath, wind)
        float noise = 0.0f, noiseHz = 2000.0f, noiseQ = 1.0f;
        float noiseDecay = 0.02f;     // seconds per hit; 0 = follows the sustain envelope
        float wander = 0.0f;          // the noise band drifts around (wind)
        // two single resonances at fixed frequencies (relative to hz): the snap's pop, a body thump
        float pop = 0.0f, popHz = 1000.0f, popDecay = 0.03f;
        float thump = 0.0f, thumpHz = 100.0f, thumpDecay = 0.05f;
        // continuous excitation of the modes: rubbing (bowed metal), impacts (rain, chains), stick-slip (creaks)
        float contSec = 0.0f;         // > 0: a one-shot of this length; 0: while the key is held
        float rub = 0.0f, particles = 0.0f, slipHz = 0.0f;
        bool drips = false;           // every impact is a falling drop that rises in pitch
        // plucked / struck string (Karplus-Strong)
        float string = 0.0f, stringT60 = 1.5f, stringBright = 0.5f;
        // tone oscillator: 1 bowed (saw through a body), 2 bowed saw (sine), 3 mains hum (buzz)
        int osc = 0;
        float oscLevel = 0.0f, oscBright = 6.0f, bodyHz = 0.0f, vibrato = 0.0f, vibRate = 5.0f, bowNoise = 0.0f;
        // pitch movement
        float glide = 0.0f, glideSec = 0.05f;   // pitch starts (1 + glide) times higher (negative: lower) and settles
        float wobble = 0.0f, wobbleHz = 0.3f;    // semitones of slow wandering
        float level = 1.0f;
    };

    enum Snd
    {
        sSnap, sClap, sGroupClap, sKneeSlap, sTongue, sChest, sFingerClick, sStomp,
        sCrate, sChair, sPlank, sLog, sWoodBlock, sBones, sDoorKnock, sWoodBox, sColLegno, sBassSlap, sBassPizz, sCelloPizz,
        sBrake, sBrakeMuted, sPipe, sAnvil, sBellPlate, sTinCan, sPotLid, sOilDrum, sJailDoor, sChains, sSpring, sGlass,
        sBowedSaw, sBowedMetal, sBowedBass, sBowedCello, sWind, sRumble, sRain, sCreak, sDrips, sHum, sThunder, sWaterphone, sGong,
        sMarimba, sBassMarimba,
        numSounds
    };

    const std::vector<SoundDef>& sounds()
    {
        static const std::vector<SoundDef> v = []
        {
            std::vector<SoundDef> d ((size_t) numSounds);
            auto& s = d;
            // ---- body percussion ----
            { auto& x = s[sSnap];   x.name = "Finger Snap"; x.hz = 2400; x.contactMs = 0.25f; x.noise = 1.0f; x.noiseHz = 2400; x.noiseQ = 1.6f; x.noiseDecay = 0.024f;
                                    x.pop = 0.55f; x.popHz = 1500; x.popDecay = 0.03f; x.thump = 0.3f; x.thumpHz = 220; x.thumpDecay = 0.02f; }
            { auto& x = s[sClap];   x.name = "Hand Clap"; x.hz = 1200; x.contactMs = 0.4f; x.hits = 4; x.hitGapMs = 9.0f; x.hitJitter = 0.3f;
                                    x.noise = 1.0f; x.noiseHz = 1200; x.noiseQ = 1.1f; x.noiseDecay = 0.012f; x.thump = 0.06f; x.thumpHz = 300; x.thumpDecay = 0.015f; }
            { auto& x = s[sGroupClap]; x = s[sClap]; x.name = "Room of Claps"; x.hits = 7; x.hitGapMs = 6.0f; x.hitJitter = 0.7f; x.noiseHz = 1000; x.noiseDecay = 0.018f; }
            { auto& x = s[sKneeSlap]; x.name = "Knee Slap"; x.hz = 140; x.contactMs = 1.2f; x.thump = 1.0f; x.thumpHz = 140; x.thumpDecay = 0.06f;
                                    x.noise = 0.5f; x.noiseHz = 900; x.noiseQ = 0.8f; x.noiseDecay = 0.03f; x.pop = 0.3f; x.popHz = 420; x.popDecay = 0.04f; }
            { auto& x = s[sTongue]; x.name = "Tongue Click"; x.hz = 1100; x.contactMs = 0.15f; x.pop = 1.0f; x.popHz = 1100; x.popDecay = 0.02f; x.glide = -0.3f; x.glideSec = 0.012f;
                                    x.noise = 0.15f; x.noiseHz = 3000; x.noiseDecay = 0.005f; }
            { auto& x = s[sChest];  x.name = "Chest Thump"; x.hz = 85; x.contactMs = 2.0f; x.thump = 1.0f; x.thumpHz = 85; x.thumpDecay = 0.09f;
                                    x.noise = 0.2f; x.noiseHz = 400; x.noiseQ = 0.7f; x.noiseDecay = 0.04f; }
            { auto& x = s[sFingerClick]; x.name = "Finger Click"; x.hz = 3000; x.contactMs = 0.1f; x.pop = 0.8f; x.popHz = 3000; x.popDecay = 0.008f;
                                    x.noise = 0.6f; x.noiseHz = 5000; x.noiseQ = 1.0f; x.noiseDecay = 0.006f; }
            { auto& x = s[sStomp];  x.name = "Foot Stomp (floorboards)"; x.hz = 120; x.contactMs = 2.5f; x.modes = Modes::hollow; x.t60 = 0.15f; x.damp = 0.8f; x.modeLevel = 0.5f;
                                    x.thump = 1.0f; x.thumpHz = 70; x.thumpDecay = 0.12f; x.noise = 0.3f; x.noiseHz = 600; x.noiseDecay = 0.05f; }
            // ---- wood ----
            { auto& x = s[sCrate];  x.name = "Wooden Crate"; x.hz = 95; x.contactMs = 1.5f; x.modes = Modes::hollow; x.t60 = 0.18f; x.damp = 0.8f;
                                    x.noise = 0.25f; x.noiseHz = 900; x.noiseDecay = 0.03f; x.thump = 0.4f; x.thumpHz = 70; x.thumpDecay = 0.05f; }
            { auto& x = s[sChair];  x.name = "Chair Knock"; x.hz = 180; x.contactMs = 0.6f; x.modes = Modes::hollow; x.t60 = 0.12f; x.damp = 0.7f;
                                    x.noise = 0.15f; x.noiseHz = 2000; x.noiseDecay = 0.01f; }
            { auto& x = s[sPlank];  x.name = "Plank"; x.hz = 260; x.contactMs = 0.8f; x.modes = Modes::bar; x.t60 = 0.25f; x.damp = 0.9f; }
            { auto& x = s[sLog];    x.name = "Log Drum"; x.hz = 220; x.contactMs = 1.5f; x.modes = Modes::wood; x.t60 = 0.45f; x.damp = 0.6f; x.thump = 0.2f; x.thumpHz = 110; x.thumpDecay = 0.05f; }
            { auto& x = s[sWoodBlock]; x.name = "Wood Block"; x.hz = 900; x.contactMs = 0.3f; x.modes = Modes::wood; x.t60 = 0.09f; x.damp = 0.5f; }
            { auto& x = s[sBones];  x.name = "Bones"; x.hz = 1500; x.contactMs = 0.2f; x.hits = 2; x.hitGapMs = 22.0f; x.hitJitter = 0.2f; x.modes = Modes::wood; x.t60 = 0.06f;
                                    x.noise = 0.3f; x.noiseHz = 4000; x.noiseDecay = 0.005f; }
            { auto& x = s[sDoorKnock]; x.name = "Door Knock"; x.hz = 150; x.contactMs = 0.8f; x.hits = 2; x.hitGapMs = 140.0f; x.hitJitter = 0.1f; x.modes = Modes::hollow;
                                    x.t60 = 0.1f; x.thump = 0.5f; x.thumpHz = 90; x.thumpDecay = 0.06f; }
            { auto& x = s[sWoodBox]; x.name = "Wooden Box (cajon)"; x.hz = 110; x.contactMs = 1.0f; x.modes = Modes::hollow; x.t60 = 0.2f; x.damp = 0.7f;
                                    x.thump = 0.7f; x.thumpHz = 75; x.thumpDecay = 0.08f; x.noise = 0.25f; x.noiseHz = 1500; x.noiseDecay = 0.04f; }
            // ---- strings ----
            { auto& x = s[sColLegno]; x.name = "Cello Col Legno"; x.hz = 130.8f; x.contactMs = 0.3f; x.string = 0.8f; x.stringT60 = 0.5f; x.stringBright = 0.4f;
                                    x.pop = 0.25f; x.popHz = 1800; x.popDecay = 0.008f; x.thump = 0.2f; x.thumpHz = 200; x.thumpDecay = 0.04f; }
            { auto& x = s[sBassSlap]; x.name = "Double Bass Body Slap"; x.hz = 95; x.contactMs = 1.0f; x.modes = Modes::hollow; x.t60 = 0.25f; x.damp = 0.7f;
                                    x.string = 0.3f; x.stringT60 = 0.8f; x.stringBright = 0.3f; x.noise = 0.3f; x.noiseHz = 800; x.noiseDecay = 0.02f; }
            { auto& x = s[sBassPizz]; x.name = "Double Bass Pizzicato"; x.hz = 41.2f; x.contactMs = 2.0f; x.string = 1.0f; x.stringT60 = 2.0f; x.stringBright = 0.3f;
                                    x.thump = 0.35f; x.thumpHz = 90; x.thumpDecay = 0.08f; }
            { auto& x = s[sCelloPizz]; x.name = "Cello Pizzicato"; x.hz = 130.8f; x.contactMs = 1.0f; x.string = 1.0f; x.stringT60 = 1.2f; x.stringBright = 0.4f;
                                    x.thump = 0.15f; x.thumpHz = 200; x.thumpDecay = 0.05f; }
            // ---- metal ----
            { auto& x = s[sBrake];  x.name = "Brake Drum"; x.hz = 520; x.contactMs = 0.25f; x.modes = Modes::brake; x.t60 = 1.6f; x.damp = 0.55f; x.spread = 0.006f;
                                    x.noise = 0.1f; x.noiseHz = 5000; x.noiseDecay = 0.01f; }
            { auto& x = s[sBrakeMuted]; x = s[sBrake]; x.name = "Brake Drum (hand muted)"; x.t60 = 0.18f; x.damp = 0.3f; }
            { auto& x = s[sPipe];   x.name = "Scaffold Pipe"; x.hz = 640; x.contactMs = 0.3f; x.modes = Modes::bar; x.t60 = 2.4f; x.damp = 0.35f; x.spread = 0.003f; }
            { auto& x = s[sAnvil];  x.name = "Anvil"; x.hz = 1150; x.contactMs = 0.15f; x.modes = Modes::anvil; x.t60 = 2.2f; x.damp = 0.4f; }
            { auto& x = s[sBellPlate]; x.name = "Bell Plate"; x.hz = 300; x.contactMs = 0.6f; x.modes = Modes::plate; x.t60 = 3.5f; x.damp = 0.45f; }
            { auto& x = s[sTinCan]; x.name = "Tin Can"; x.hz = 880; x.contactMs = 0.3f; x.modes = Modes::can; x.t60 = 0.25f; x.damp = 0.5f;
                                    x.noise = 0.2f; x.noiseHz = 3000; x.noiseDecay = 0.02f; }
            { auto& x = s[sPotLid]; x.name = "Pot Lid"; x.hz = 420; x.contactMs = 0.4f; x.modes = Modes::lid; x.t60 = 1.2f; x.damp = 0.6f; x.spread = 0.01f; }
            { auto& x = s[sOilDrum]; x.name = "Oil Drum"; x.hz = 110; x.contactMs = 2.0f; x.modes = Modes::drum; x.t60 = 0.9f; x.damp = 0.6f;
                                    x.thump = 0.5f; x.thumpHz = 70; x.thumpDecay = 0.15f; x.noise = 0.15f; x.noiseHz = 600; x.noiseDecay = 0.05f; }
            { auto& x = s[sJailDoor]; x.name = "Jail Door Clang"; x.hz = 170; x.contactMs = 0.6f; x.modes = Modes::dense; x.t60 = 2.0f; x.damp = 0.5f;
                                    x.contSec = 0.35f; x.particles = 220.0f; x.thump = 0.3f; x.thumpHz = 60; x.thumpDecay = 0.2f; }
            { auto& x = s[sChains]; x.name = "Chain Rattle"; x.hz = 2400; x.strike = 0.2f; x.contactMs = 0.2f; x.modes = Modes::can; x.t60 = 0.08f; x.damp = 0.3f;
                                    x.contSec = 0.7f; x.particles = 160.0f; }
            { auto& x = s[sSpring]; x.name = "Spring Boing"; x.hz = 300; x.contactMs = 0.5f; x.modes = Modes::spring; x.t60 = 1.0f; x.damp = 0.6f;
                                    x.glide = 0.6f; x.glideSec = 0.12f; x.wobble = 0.8f; x.wobbleHz = 9.0f; }
            { auto& x = s[sGlass];  x.name = "Glass Bottle"; x.hz = 1400; x.contactMs = 0.2f; x.modes = Modes::glass; x.t60 = 0.7f; x.damp = 0.5f; }
            // ---- sound-space textures ----
            { auto& x = s[sBowedSaw]; x.name = "Bowed Saw"; x.hz = 523.3f; x.sustain = true; x.attack = 0.35f; x.release = 0.8f; x.strike = 0.0f;
                                    x.osc = 2; x.oscLevel = 1.0f; x.vibrato = 28.0f; x.vibRate = 5.2f; x.wobble = 0.08f; x.wobbleHz = 0.4f; }
            { auto& x = s[sBowedMetal]; x.name = "Bowed Metal"; x.hz = 300; x.sustain = true; x.attack = 0.8f; x.release = 1.5f; x.strike = 0.0f;
                                    x.modes = Modes::plate; x.t60 = 4.0f; x.damp = 0.3f; x.rub = 1.0f; x.wobble = 0.05f; x.wobbleHz = 0.2f; }
            { auto& x = s[sBowedBass]; x.name = "Bowed Double Bass"; x.hz = 41.2f; x.sustain = true; x.attack = 0.25f; x.release = 0.4f; x.strike = 0.0f;
                                    x.osc = 1; x.oscLevel = 1.0f; x.oscBright = 5.0f; x.bodyHz = 110; x.vibrato = 6.0f; x.vibRate = 4.8f; x.bowNoise = 0.2f; }
            { auto& x = s[sBowedCello]; x = s[sBowedBass]; x.name = "Bowed Cello"; x.hz = 130.8f; x.bodyHz = 230; x.oscBright = 7.0f; x.vibrato = 12.0f; x.vibRate = 5.4f; }
            { auto& x = s[sWind];   x.name = "Wind"; x.hz = 700; x.sustain = true; x.attack = 1.2f; x.release = 2.0f; x.strike = 0.0f;
                                    x.noise = 1.0f; x.noiseHz = 700; x.noiseQ = 3.0f; x.noiseDecay = 0.0f; x.wander = 0.8f; }
            { auto& x = s[sRumble]; x.name = "Distant Rumble"; x.hz = 70; x.sustain = true; x.attack = 1.5f; x.release = 2.5f; x.strike = 0.0f;
                                    x.noise = 1.0f; x.noiseHz = 70; x.noiseQ = 0.7f; x.noiseDecay = 0.0f; x.wander = 0.5f; }
            { auto& x = s[sRain];   x.name = "Rain on a Tin Roof"; x.hz = 3200; x.sustain = true; x.attack = 0.8f; x.release = 1.5f; x.strike = 0.0f;
                                    x.modes = Modes::can; x.t60 = 0.05f; x.damp = 0.3f; x.particles = 90.0f; x.noise = 0.25f; x.noiseHz = 6000; x.noiseQ = 0.7f; x.noiseDecay = 0.0f; }
            { auto& x = s[sCreak];  x.name = "Creaking Wood"; x.hz = 330; x.strike = 0.0f; x.modes = Modes::wood; x.t60 = 0.06f; x.damp = 0.4f; x.contSec = 1.4f; x.slipHz = 35.0f; }
            { auto& x = s[sDrips];  x.name = "Cave Drips"; x.hz = 900; x.sustain = true; x.attack = 0.05f; x.release = 2.0f; x.strike = 0.0f;
                                    x.modes = Modes::single; x.t60 = 0.08f; x.particles = 3.0f; x.drips = true; }
            { auto& x = s[sHum];    x.name = "Mains Hum"; x.hz = 50; x.sustain = true; x.attack = 0.3f; x.release = 0.5f; x.strike = 0.0f;
                                    x.osc = 3; x.oscLevel = 1.0f; x.oscBright = 12.0f; x.wobble = 0.02f; x.wobbleHz = 0.5f; }
            { auto& x = s[sThunder]; x.name = "Thunder Sheet"; x.hz = 60; x.contactMs = 4.0f; x.strike = 0.6f; x.modes = Modes::dense; x.t60 = 3.0f; x.damp = 0.7f;
                                    x.rub = 1.0f; x.contSec = 2.2f; }
            { auto& x = s[sWaterphone]; x.name = "Waterphone"; x.hz = 700; x.sustain = true; x.attack = 1.0f; x.release = 3.0f; x.strike = 0.0f;
                                    x.modes = Modes::plate; x.t60 = 3.0f; x.damp = 0.4f; x.rub = 0.7f; x.wobble = 1.2f; x.wobbleHz = 0.35f; }
            { auto& x = s[sGong];   x.name = "Deep Gong"; x.hz = 75; x.contactMs = 4.0f; x.modes = Modes::gong; x.t60 = 6.0f; x.damp = 0.35f; x.spread = 0.004f; }
            // ---- tuned, for the chromatic sources ----
            { auto& x = s[sMarimba]; x.name = "Marimba"; x.hz = 261.6f; x.contactMs = 1.5f; x.modes = Modes::marimba; x.t60 = 0.9f; x.damp = 0.9f; x.spread = 0.0f; }
            { auto& x = s[sBassMarimba]; x = s[sMarimba]; x.name = "Bass Marimba"; x.hz = 65.4f; x.contactMs = 3.0f; x.t60 = 1.5f; }
            return d;
        }();
        return v;
    }

    // ---- the Junk Kit: one object per key ---------------------------------------------------------------
    struct KitSlot { int sound = -1; float semis = 0.0f, gain = 1.0f, pan = 0.0f; };

    const std::array<KitSlot, 128>& kitMap()
    {
        static const std::array<KitSlot, 128> k = []
        {
            std::array<KitSlot, 128> m {};
            auto set = [&m] (int note, int snd, float semis, float gain, float pan) { m[(size_t) note] = { snd, semis, gain, pan }; };
            using J = Junkyard;
            set (J::kneeSlap,    sKneeSlap, 0, 0.9f, -0.1f);
            set (J::snapTight,   sSnap, 2, 0.85f, 0.15f);
            set (J::snapFat,     sSnap, -4, 0.9f, -0.15f);
            set (J::handClap,    sClap, 0, 0.85f, 0.0f);
            set (J::snapBright,  sSnap, 6, 0.75f, 0.25f);
            set (J::thighSlap,   sKneeSlap, -3, 0.9f, 0.1f);
            set (J::tongueClick, sTongue, 0, 0.7f, 0.0f);
            set (J::chestThump,  sChest, 0, 1.0f, 0.0f);
            set (J::snapSoft,    sSnap, -2, 0.55f, -0.25f);
            set (J::groupClap,   sGroupClap, 0, 0.85f, 0.0f);
            set (J::fingerClick, sFingerClick, 0, 0.6f, 0.2f);
            set (J::footStomp,   sStomp, 0, 1.0f, 0.0f);

            set (J::crate,       sCrate, 0, 0.9f, -0.2f);
            set (J::chairKnock,  sChair, 0, 0.8f, 0.25f);
            set (J::plank,       sPlank, 0, 0.8f, -0.3f);
            set (J::logLow,      sLog, 0, 0.85f, -0.15f);
            set (J::logHigh,     sLog, 7, 0.8f, 0.15f);
            set (J::woodBlock,   sWoodBlock, 0, 0.7f, 0.35f);
            set (J::bones,       sBones, 0, 0.7f, 0.3f);
            set (J::doorKnock,   sDoorKnock, 0, 0.85f, -0.35f);
            set (J::woodBox,     sWoodBox, 0, 0.95f, 0.0f);
            set (J::colLegno,    sColLegno, 0, 0.75f, -0.2f);
            set (J::bassBodySlap, sBassSlap, 0, 0.9f, 0.1f);
            set (J::bassPizz,    sBassPizz, 0, 1.0f, 0.0f);

            set (J::brakeDrum,   sBrake, 0, 0.7f, 0.3f);
            set (J::brakeMuted,  sBrakeMuted, 0, 0.75f, 0.3f);
            set (J::pipe,        sPipe, 0, 0.6f, -0.4f);
            set (J::anvil,       sAnvil, 0, 0.55f, 0.45f);
            set (J::bellPlate,   sBellPlate, 0, 0.6f, -0.3f);
            set (J::tinCan,      sTinCan, 0, 0.7f, 0.4f);
            set (J::potLid,      sPotLid, 0, 0.65f, -0.45f);
            set (J::oilDrum,     sOilDrum, 0, 0.9f, 0.0f);
            set (J::jailDoor,    sJailDoor, 0, 0.8f, -0.1f);
            set (J::chains,      sChains, 0, 0.55f, 0.35f);
            set (J::springBoing, sSpring, 0, 0.6f, 0.2f);
            set (J::glassBottle, sGlass, 0, 0.55f, -0.35f);

            set (J::bowedSaw,    sBowedSaw, 0, 0.6f, 0.0f);
            set (J::bowedMetal,  sBowedMetal, 0, 0.6f, 0.0f);
            set (J::bowedBass,   sBowedBass, 0, 0.8f, -0.1f);
            set (J::bowedCello,  sBowedCello, 0, 0.7f, 0.1f);
            set (J::wind,        sWind, 0, 0.7f, 0.0f);
            set (J::rumble,      sRumble, 0, 0.9f, 0.0f);
            set (J::rain,        sRain, 0, 0.6f, 0.0f);
            set (J::creak,       sCreak, 0, 0.7f, -0.3f);
            set (J::drips,       sDrips, 0, 0.6f, 0.0f);
            set (J::hum,         sHum, 0, 0.45f, 0.0f);
            set (J::thunderSheet, sThunder, 0, 0.8f, 0.0f);
            set (J::waterphone,  sWaterphone, 0, 0.6f, 0.0f);
            set (J::deepGong,    sGong, 0, 0.8f, 0.0f);
            return m;
        }();
        return k;
    }

    /** The chromatic sources (source parameter 1..): one sound across the keyboard. */
    const std::vector<int>& chromaticSounds()
    {
        static const std::vector<int> c { sMarimba, sBassMarimba, sBrake, sPipe, sBellPlate, sGlass, sLog, sWoodBlock, sBassPizz, sCelloPizz, sColLegno,
                                          sBowedBass, sBowedCello, sBowedSaw, sBowedMetal, sWaterphone, sWind, sGong, sOilDrum, sTinCan, sPotLid };
        return c;
    }

    /** Zero-delay-feedback state-variable filter (band-pass output). */
    struct Svf
    {
        float g = 0.1f, k = 1.0f, ic1 = 0.0f, ic2 = 0.0f;
        void set (float hz, float q, float sr) noexcept
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, sr * 0.45f, hz) / sr);
            k = 1.0f / juce::jmax (0.3f, q);
        }
        float bp (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = (g * v3 + ic1) / (1.0f + g * (g + k));
            const float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v1 * k;   // unity gain at the centre
        }
    };

    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    struct Globals { float decay = 1.0f, tone = 0.0f, humanize = 0.3f, rattle = 0.0f, width = 1.0f; };
}

// =========================================================================================================
//  Voice
// =========================================================================================================
struct Junkyard::Voice
{
    static constexpr int maxModes = 20;

    bool active = false, held = false, fading = false;
    int note = -1, sound = 0;
    juce::uint32 order = 0;
    double sr = 48000.0;
    juce::Random rnd;
    const SoundDef* d = nullptr;

    float freqScale = 1.0f, vel = 1.0f, gain = 1.0f, panL = 1.0f, panR = 1.0f;
    int age = 0, quietBlocks = 0;
    float fadeGain = 1.0f;

    // modes
    int nModes = 0;
    float re[maxModes] {}, im[maxModes] {}, cr[maxModes] {}, ci[maxModes] {}, rad[maxModes] {}, w0[maxModes] {}, ampL[maxModes] {}, ampR[maxModes] {};
    float pitchMod = 1.0f, lastPitchMod = 1.0f;

    // strike
    int hitIndex = 0, nextHitAt = 0, pulsePos = 0, pulseLen = 1;
    float pulseAmp = 0.0f;

    // noise band (two filters: stereo for wind)
    Svf nf[2];
    float noiseEnv = 0.0f, noiseK = 0.0f, noiseCentre = 1000.0f, wanderPos = 0.0f, wanderVel = 0.0f;
    int nfUpdate = 0;

    // continuous excitation
    float contEnv = 0.0f, contA = 0.0f, contR = 0.0f, rubLp = 0.0f, slipPhase = 0.0f, rattleEnv = 0.0f, rattleK = 0.0f;
    float dripScale = 1.0f;
    int dripAge = 1 << 20;

    // string
    std::vector<float> line;
    int lineW = 0;
    float lineDelay = 10.0f, lineG = 0.99f, lineA = 0.5f, linePrev = 0.0f;

    // oscillator
    float phase = 0.0f, vibPhase = 0.0f, oscLp = 0.0f, bowLp = 0.0f;
    Svf body[3];

    // pitch movement
    float wobblePhase = 0.0f, jitter = 0.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        line.assign ((size_t) (sampleRate / 20.0) + 8, 0.0f);
    }

    float level() const noexcept { return d == nullptr ? 0.0f : gain * vel * fadeGain; }

    void start (int noteNumber, int snd, float baseHz, float velocity, float voiceGain, float pan, const Globals& gl, juce::uint32 seed)
    {
        rnd.setSeed ((juce::int64) seed * 2654435761u + 17);
        note = noteNumber;
        sound = snd;
        d = &sounds()[(size_t) snd];
        active = true; held = true; fading = false;
        age = 0; quietBlocks = 0; fadeGain = 1.0f;
        vel = velocity;
        const float h = gl.humanize;
        auto bip = [this] { return rnd.nextFloat() * 2.0f - 1.0f; };

        const float humanCents = bip() * h * 22.0f;
        const float f = baseHz * std::pow (2.0f, humanCents / 1200.0f);
        freqScale = f / d->hz;
        gain = voiceGain * (1.0f - 0.2f * h * rnd.nextFloat()) * (0.18f + 0.82f * std::pow (velocity, 1.4f));
        const float p = juce::jlimit (-1.0f, 1.0f, pan * gl.width);
        panL = std::sqrt (0.5f * (1.0f - p)) * 1.4142f;
        panR = std::sqrt (0.5f * (1.0f + p)) * 1.4142f;

        const float decayMul = gl.decay;
        // ---- modes ----
        nModes = 0;
        auto addMode = [&] (float hz, float t60, float amp, float modePan)
        {
            if (nModes >= maxModes || amp <= 0.0f) return;
            const int i = nModes++;
            w0[i] = twoPi * hz / (float) sr;
            rad[i] = std::exp (-6.9078f / (juce::jmax (0.004f, t60) * (float) sr));
            const float mp = juce::jlimit (-1.0f, 1.0f, p + modePan * gl.width);
            ampL[i] = amp * std::sqrt (0.5f * (1.0f - mp)) * 1.4142f;
            ampR[i] = amp * std::sqrt (0.5f * (1.0f + mp)) * 1.4142f;
            re[i] = im[i] = 0.0f;
        };
        const auto& ms = modeSet (d->modes).m;
        if (! ms.empty())
        {
            float norm = 0.0f;
            for (auto& [r, a] : ms) norm += a * a;
            norm = d->modeLevel / std::sqrt (juce::jmax (1.0e-6f, norm));
            const float spread = d->spread + 0.004f * h;
            for (auto& [r, a] : ms)
            {
                const float ratio = r * (1.0f + spread * bip());
                addMode (d->hz * freqScale * ratio, d->t60 * decayMul * std::pow (ratio, -d->damp), a * norm, (ms.size() > 1 ? 0.6f : 0.0f) * bip());
            }
        }
        if (d->pop > 0.0f)   addMode (d->popHz * freqScale, d->popDecay * std::sqrt (decayMul), d->pop, 0.0f);
        if (d->thump > 0.0f) addMode (d->thumpHz * freqScale, d->thumpDecay * std::sqrt (decayMul), d->thump, 0.0f);
        pitchMod = lastPitchMod = 1.0f + d->glide;
        updateCoefficients();

        // ---- strike ----
        hitIndex = 0; nextHitAt = 0; pulsePos = 1 << 30; pulseAmp = 0.0f;
        noiseEnv = 0.0f;
        noiseK = d->noiseDecay > 0.0f ? std::exp (-6.9078f / (d->noiseDecay * std::sqrt (decayMul) * (float) sr)) : 0.0f;

        // ---- noise band ----
        noiseCentre = d->noiseHz * freqScale;
        wanderPos = 0.0f; wanderVel = 0.0f; nfUpdate = 0;
        for (auto& s : nf) { s.ic1 = s.ic2 = 0.0f; s.set (noiseCentre, d->noiseQ, (float) sr); }

        // ---- continuous excitation ----
        contEnv = 0.0f;
        contA = 1.0f - std::exp (-1.0f / (juce::jmax (0.0005f, d->attack * attackScale) * (float) sr));
        contR = 1.0f - std::exp (-1.0f / (juce::jmax (0.01f, d->release * decayMul) * 0.25f * (float) sr));
        rubLp = 0.0f; slipPhase = 0.0f; dripAge = 1 << 20; dripScale = 1.0f;
        const bool modal = nModes > 0 && ! d->sustain && d->rub <= 0.0f;
        rattleEnv = modal ? gl.rattle : 0.0f;
        rattleK = std::exp (-6.9078f / (0.35f * (float) sr));

        // ---- string ----
        if (d->string > 0.0f)
        {
            const float sf = d->hz * freqScale;
            lineA = 0.5f + 0.45f * d->stringBright;
            lineDelay = juce::jlimit (2.0f, (float) line.size() - 4.0f, (float) sr / sf - (1.0f - lineA));
            lineG = std::pow (10.0f, -3.0f * (lineDelay / (float) sr) / juce::jmax (0.05f, d->stringT60 * decayMul));
            std::fill (line.begin(), line.end(), 0.0f);
            // a plucked shape: filtered noise, softer for a bigger contact
            const int n = (int) lineDelay;
            const float lp = juce::jlimit (0.03f, 0.95f, 0.04f + 0.6f * std::pow (d->stringBright, 1.5f) * (0.6f + 0.4f * velocity));
            float z = 0.0f, mean = 0.0f;
            for (int i = 0; i < n; ++i) { z += lp * (bip() - z); line[(size_t) i] = z; mean += z; }
            mean /= (float) juce::jmax (1, n);
            for (int i = 0; i < n; ++i) line[(size_t) i] = (line[(size_t) i] - mean) * 2.0f;
            lineW = n;
            linePrev = 0.0f;
        }

        // ---- oscillator ----
        phase = rnd.nextFloat(); vibPhase = rnd.nextFloat(); oscLp = 0.0f; bowLp = 0.0f;
        if (d->bodyHz > 0.0f)
        {
            const float bh = d->bodyHz * std::sqrt (freqScale);
            body[0].set (bh, 1.6f, (float) sr); body[1].set (bh * 2.6f, 2.0f, (float) sr); body[2].set (bh * 5.4f, 2.5f, (float) sr);
            for (auto& b : body) b.ic1 = b.ic2 = 0.0f;
        }
        wobblePhase = rnd.nextFloat(); jitter = 0.0f;
    }

    void updateCoefficients() noexcept
    {
        const float pm = pitchMod * dripScale;
        for (int i = 0; i < nModes; ++i)
        {
            const float w = w0[i] * pm;
            if (w >= 3.0f) { cr[i] = ci[i] = 0.0f; continue; }   // above Nyquist: silent
            cr[i] = rad[i] * std::cos (w);
            ci[i] = rad[i] * std::sin (w);
        }
        lastPitchMod = pm;
    }

    void release()
    {
        held = false;
    }

    /** Adds this voice into L/R. Returns the block's peak (to know when it has died away). */
    float render (float* L, float* R, int n)
    {
        const float fsr = (float) sr;
        const SoundDef& s = *d;
        const float g = gain;
        float peak = 0.0f;
        const bool moving = s.glide != 0.0f || s.wobble > 0.0f || s.drips;
        const float hitGap = s.hitGapMs * 0.001f * fsr;
        const float contactBase = juce::jmax (0.05f, s.contactMs * (1.35f - 0.5f * vel)) * 0.001f * fsr;

        for (int i = 0; i < n; ++i, ++age)
        {
            // ---- pitch movement (every 16 samples) ----
            if (moving && (age & 15) == 0)
            {
                float pm = 1.0f;
                if (s.glide != 0.0f) pm *= 1.0f + s.glide * std::exp (-(float) age / (s.glideSec * fsr));
                if (s.wobble > 0.0f)
                {
                    wobblePhase += 16.0f * s.wobbleHz / fsr;
                    if (wobblePhase >= 1.0f) wobblePhase -= 1.0f;
                    jitter += 0.02f * ((rnd.nextFloat() * 2.0f - 1.0f) - jitter);
                    pm *= std::pow (2.0f, s.wobble * (0.7f * std::sin (twoPi * wobblePhase) + 0.6f * jitter) / 12.0f);
                }
                if (s.drips)
                {
                    // a drop: its pitch rises quickly as the bubble collapses
                    dripScale = dripScale0 * (1.0f + 0.9f * (1.0f - std::exp (-(float) dripAge / (0.02f * fsr))));
                }
                pitchMod = pm;
                if (std::abs (pitchMod * dripScale - lastPitchMod) > 1.0e-5f) updateCoefficients();
            }

            // ---- strike ----
            float exc = 0.0f;
            if (hitIndex < s.hits && age >= nextHitAt && s.strike > 0.0f)
            {
                const float hv = hitIndex == 0 ? 1.0f : 0.55f + 0.4f * rnd.nextFloat();
                pulseLen = juce::jmax (1, (int) (contactBase * (0.85f + 0.3f * rnd.nextFloat())));
                pulseAmp = s.strike * hv * juce::MathConstants<float>::pi / (2.0f * (float) pulseLen);
                pulsePos = 0;
                noiseEnv = s.noise * hv;
                if (hitIndex == s.hits - 1 && s.hits > 1) noiseK = std::pow (noiseK, 0.25f);   // the last clap rings on
                ++hitIndex;
                nextHitAt = age + juce::jmax (1, (int) (hitGap * (1.0f + s.hitJitter * (rnd.nextFloat() * 2.0f - 1.0f))));
            }
            if (pulsePos < pulseLen)
            {
                exc += pulseAmp * std::sin (juce::MathConstants<float>::pi * ((float) pulsePos + 0.5f) / (float) pulseLen);
                ++pulsePos;
            }

            // ---- continuous excitation envelope ----
            if (s.sustain)
                contEnv += ((held ? 1.0f : 0.0f) - contEnv) * (held ? contA : contR);
            else if (s.contSec > 0.0f)
            {
                const float t = (float) age / (s.contSec * fsr);
                contEnv = t < 0.15f ? t / 0.15f : std::exp (-4.0f * (t - 0.15f));
            }
            else
                contEnv = 0.0f;

            const float white = rnd.nextFloat() * 2.0f - 1.0f;
            if (s.rub > 0.0f)
            {
                rubLp += 0.25f * (white - rubLp);
                exc += s.rub * contEnv * rubLp * 0.05f;
            }
            if (s.particles > 0.0f && contEnv > 0.001f && rnd.nextFloat() < s.particles * contEnv / fsr)
            {
                if (s.drips)
                {
                    dripScale0 = 0.6f + 0.9f * rnd.nextFloat();
                    dripAge = 0;
                    for (int m = 0; m < nModes; ++m) re[m] = im[m] = 0.0f;
                    dripScale = dripScale0;
                    updateCoefficients();
                    exc += 0.6f + 0.4f * rnd.nextFloat();
                }
                else
                    exc += (0.15f + 0.85f * rnd.nextFloat()) * 0.35f;
            }
            ++dripAge;
            if (s.slipHz > 0.0f && contEnv > 0.001f)
            {
                slipPhase += s.slipHz * (0.6f + 0.8f * contEnv + 0.3f * jitter) / fsr;
                if (rnd.nextFloat() < 0.0004f) jitter = rnd.nextFloat() * 2.0f - 1.0f;
                if (slipPhase >= 1.0f) { slipPhase -= 1.0f; exc += 0.5f * contEnv * (0.7f + 0.3f * rnd.nextFloat()); }
            }
            if (rattleEnv > 0.0005f)
            {
                if (rnd.nextFloat() < 300.0f / fsr) exc += rattleEnv * 0.25f * rnd.nextFloat();
                rattleEnv *= rattleK;
            }

            // ---- modes ----
            float l = 0.0f, r = 0.0f;
            for (int m = 0; m < nModes; ++m)
            {
                const float a = re[m], b = im[m];
                re[m] = a * cr[m] - b * ci[m] + exc;
                im[m] = a * ci[m] + b * cr[m];
                l += im[m] * ampL[m];
                r += im[m] * ampR[m];
            }

            // ---- direct (mono, panned) ----
            float mono = 0.0f;
            if (s.noise > 0.0f)
            {
                if (s.noiseDecay <= 0.0f) noiseEnv = s.noise * contEnv;
                if (s.wander > 0.0f && (++nfUpdate & 31) == 0)
                {
                    wanderVel += 0.03f * ((rnd.nextFloat() * 2.0f - 1.0f) - wanderVel);
                    wanderPos = juce::jlimit (-1.0f, 1.0f, wanderPos + 0.04f * wanderVel);
                    const float c = noiseCentre * std::pow (2.0f, s.wander * 1.6f * wanderPos);
                    nf[0].set (c, s.noiseQ * (1.0f + 0.4f * wanderVel), fsr);
                    nf[1].set (c * 1.12f, s.noiseQ, fsr);
                }
                if (noiseEnv > 1.0e-5f)
                {
                    if (s.wander > 0.0f)
                    {
                        const float gust = 0.65f + 0.35f * wanderPos;
                        l += nf[0].bp (white) * noiseEnv * gust * panL;
                        r += nf[1].bp (rnd.nextFloat() * 2.0f - 1.0f) * noiseEnv * gust * panR;
                    }
                    else
                        mono += nf[0].bp (white) * noiseEnv;
                }
                if (s.noiseDecay > 0.0f) noiseEnv *= noiseK;
            }
            if (s.string > 0.0f)
            {
                const int size = (int) line.size();
                float rp = (float) lineW - lineDelay;
                if (rp < 0.0f) rp += (float) size;
                const int i0 = (int) rp;
                const float fr = rp - (float) i0;
                const float y = line[(size_t) i0] + fr * (line[(size_t) ((i0 + 1) % size)] - line[(size_t) i0]);
                const float out = lineG * (lineA * y + (1.0f - lineA) * linePrev);
                linePrev = y;
                line[(size_t) lineW] = out;
                lineW = (lineW + 1) % size;
                mono += y * s.string * 0.5f;
            }
            if (s.osc > 0)
            {
                vibPhase += s.vibRate / fsr;
                if (vibPhase >= 1.0f) vibPhase -= 1.0f;
                const float vibDepth = s.vibrato * juce::jmin (1.0f, (float) age / (0.6f * fsr));
                const float f = s.hz * freqScale * pitchMod * std::pow (2.0f, vibDepth * std::sin (twoPi * vibPhase) / 1200.0f);
                const float dt = juce::jmin (0.45f, f / fsr);
                phase += dt;
                if (phase >= 1.0f) phase -= 1.0f;
                float o = 0.0f;
                if (s.osc == 2)
                    o = std::sin (twoPi * phase) + 0.06f * std::sin (2.0f * twoPi * phase);
                else
                {
                    o = 2.0f * phase - 1.0f - polyBlep (phase, dt);   // saw
                    if (s.osc == 3) o = o * 0.7f + 0.3f * (phase < 0.5f ? 1.0f : -1.0f);
                }
                const float cutoff = juce::jmin (0.95f, twoPi * f * s.oscBright / fsr);
                oscLp += cutoff * (o - oscLp);
                float v = oscLp;
                if (s.bodyHz > 0.0f)
                {
                    bowLp += 0.5f * (white - bowLp);
                    const float bowed = v + s.bowNoise * (white - bowLp) * (0.5f + 0.5f * (phase < 0.2f ? 1.0f : 0.0f));
                    v = 0.35f * bowed + 0.9f * body[0].bp (bowed) + 0.7f * body[1].bp (bowed) + 0.45f * body[2].bp (bowed);
                }
                mono += v * s.oscLevel * contEnv;
            }

            l += mono * panL;
            r += mono * panR;

            if (fading) fadeGain *= 0.995f;
            const float k = g * fadeGain;
            l *= k; r *= k;
            L[i] += l;
            R[i] += r;
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
        }
        return peak;
    }

    /** True once nothing more will come out. */
    bool finished (float blockPeak, int blockSize)
    {
        if (fading && fadeGain < 1.0e-3f) return true;
        const bool exciting = (d->sustain && (held || contEnv > 1.0e-4f)) || (d->contSec > 0.0f && age < (int) (d->contSec * sr * 1.6))
                              || (d->strike > 0.0f && hitIndex < d->hits) || age < (int) (0.05 * sr);
        if (exciting) { quietBlocks = 0; return false; }
        quietBlocks = blockPeak < 2.0e-5f ? quietBlocks + blockSize : 0;
        return quietBlocks > (int) (0.05 * sr) || age > (int) (30.0 * sr);
    }

    float dripScale0 = 1.0f;
    float attackScale = 1.0f;   // the calibration skips the slow fade-ins
};

// =========================================================================================================
//  Calibration: every sound is rendered once offline and scaled to a common loudness
// =========================================================================================================
static const std::vector<float>& soundGains()
{
    static std::once_flag once;
    static std::vector<float> gains;
    std::call_once (once, []
    {
        gains.assign ((size_t) numSounds, 1.0f);
        const double sr = 48000.0;
        Junkyard::Voice v;
        v.prepare (sr);
        v.attackScale = 0.01f;
        Globals gl;
        gl.humanize = 0.0f;
        const int n = (int) (1.0 * sr), block = 256;
        std::vector<float> L ((size_t) block), R ((size_t) block);
        for (int s = 0; s < numSounds; ++s)
        {
            const auto& d = sounds()[(size_t) s];
            v.start (60, s, d.hz, 0.8f, 1.0f, 0.0f, gl, 7);
            double sum = 0.0;
            float pk = 0.0f;
            int counted = 0;
            for (int pos = 0; pos < n; pos += block)
            {
                std::fill (L.begin(), L.end(), 0.0f);
                std::fill (R.begin(), R.end(), 0.0f);
                v.render (L.data(), R.data(), block);
                for (int i = 0; i < block; ++i)
                {
                    const float x = 0.5f * (L[(size_t) i] + R[(size_t) i]);
                    pk = juce::jmax (pk, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]));
                    if (pos + i > (int) (0.4 * sr)) { sum += (double) x * x; ++counted; }
                }
            }
            const float rmsLevel = (float) std::sqrt (sum / juce::jmax (1, counted));
            // percussion: by its peak; sustained textures: by their steady level
            // (sparse drips are percussion too: a window might hold none of them)
            const bool steady = d.sustain && ! d.drips;
            float gn = steady ? 0.14f / juce::jmax (1.0e-5f, rmsLevel) : 0.5f / juce::jmax (1.0e-5f, pk);
            if (steady) gn = juce::jmin (gn, 0.7f / juce::jmax (1.0e-5f, pk));
            gains[(size_t) s] = juce::jlimit (0.01f, 400.0f, gn);
        }
    });
    return gains;
}

// =========================================================================================================
//  Parameters & presets
// =========================================================================================================
juce::StringArray Junkyard::sourceNames()
{
    juce::StringArray n { "Junk Kit (a different thing on every key)" };
    for (int s : chromaticSounds()) n.add (sounds()[(size_t) s].name);
    return n;
}

juce::String Junkyard::kitKeyName (int note)
{
    if (! juce::isPositiveAndBelow (note, 128)) return {};
    const auto& k = kitMap()[(size_t) note];
    if (k.sound < 0) return {};
    juce::String name = sounds()[(size_t) k.sound].name;
    switch (note)
    {
        case snapTight: return "Finger Snap (tight)";
        case snapFat: return "Finger Snap (fat)";
        case snapBright: return "Finger Snap (bright)";
        case snapSoft: return "Finger Snap (soft)";
        case thighSlap: return "Thigh Slap";
        case logLow: return "Log Drum (low)";
        case logHigh: return "Log Drum (high)";
        default: return name;
    }
}

static Layout junkLayout()
{
    Layout l;
    addChoice  (l, "source", "Source", Junkyard::sourceNames(), 0);
    addDb      (l, "volume", "Volume", -24.0f, 12.0f, 0.0f);
    addFloat   (l, "tune", "Tune", -12.0f, 12.0f, 0.0f, "st", 0.0f, 1);
    addFloat   (l, "decay", "Decay", 0.25f, 4.0f, 1.0f, "x", 1.0f, 2);
    addFloat   (l, "tone", "Tone", -1.0f, 1.0f, 0.0f, "", 0.0f, 2);
    addPercent (l, "humanize", "Humanize", 0.3f);
    addPercent (l, "rattle", "Loose Junk Rattle", 0.0f);
    addFloat   (l, "width", "Width", 0.0f, 1.5f, 1.0f, "", 0.0f, 2);
    addPercent (l, "drive", "Grit", 0.0f);
    addChoice  (l, "space", "Space", roomir::spaceNames(), (int) roomir::concreteRoom);
    addPercent (l, "spaceMix", "Space Amount", 0.25f);
    addFloat   (l, "size", "Space Size", 0.5f, 2.0f, 1.0f, "x", 1.0f, 2);
    return l;
}

namespace
{
    struct JunkPreset { const char* name; const char* description; std::vector<std::pair<const char*, float>> values; };

    int sourceIndex (int sound)
    {
        const auto& c = chromaticSounds();
        for (size_t i = 0; i < c.size(); ++i) if (c[i] == sound) return (int) i + 1;
        return 0;
    }

    const std::vector<JunkPreset>& junkPresets()
    {
        using S = roomir::Space;
        static const std::vector<JunkPreset> p {
            { "Junkyard (concrete storeroom)", "The whole junk kit in a bare concrete room: brake drums, pipes, crates and chairs hit with whatever was lying around (Bone Machine-style)",
              { { "space", S::concreteRoom }, { "spaceMix", 0.3f }, { "humanize", 0.45f }, { "rattle", 0.2f }, { "drive", 0.15f } } },
            { "Swordfish Parade", "Marimba-bright wood and metal, a parade-band swagger, in a wooden studio (Swordfishtrombones-style)",
              { { "space", S::woodenStudio }, { "spaceMix", 0.2f }, { "humanize", 0.35f }, { "tone", 0.15f } } },
            { "Snaps & Claps (dry)", "Finger snaps, claps, slaps and clicks, close and dry: drop the finger-snap loops on it",
              { { "space", S::vocalBooth }, { "spaceMix", 0.12f }, { "humanize", 0.35f } } },
            { "Snaps in a Big Room", "The same snaps and claps in a large live room",
              { { "space", S::bigLiveRoom }, { "spaceMix", 0.3f }, { "humanize", 0.35f } } },
            { "Tin Shed Clatter", "Everything rattles: loose junk in a corrugated-iron shed",
              { { "space", S::tinShed }, { "spaceMix", 0.35f }, { "rattle", 0.5f }, { "humanize", 0.5f }, { "drive", 0.1f } } },
            { "Cave Sound Space", "For game levels: everything far away in a dripping cave, long tails",
              { { "space", S::cave }, { "spaceMix", 0.55f }, { "decay", 1.5f }, { "tone", -0.2f } } },
            { "Night Forest Space", "For game levels: wind, creaks and distant knocks among the trees",
              { { "space", S::forest }, { "spaceMix", 0.45f }, { "tone", -0.1f } } },
            { "Car Park Clang", "Metal hits with a long concrete flutter",
              { { "space", S::carPark }, { "spaceMix", 0.4f }, { "humanize", 0.4f } } },
            { "Stairwell Ghosts", "Bowed metal, drips and slow hits climbing a concrete stairwell",
              { { "space", S::stairwell }, { "spaceMix", 0.5f }, { "decay", 1.3f } } },
            { "Dusty Marimba", "A marimba with soft mallets, a little worn, in a wooden room",
              { { "source", (float) sourceIndex (sMarimba) }, { "space", S::woodenStudio }, { "spaceMix", 0.2f }, { "humanize", 0.25f }, { "drive", 0.1f } } },
            { "Bass Marimba", "Deep wooden bars for slow, swaying bass lines",
              { { "source", (float) sourceIndex (sBassMarimba) }, { "space", S::woodenStudio }, { "spaceMix", 0.18f } } },
            { "Tuned Brake Drums", "Brake drums you can play tunes on",
              { { "source", (float) sourceIndex (sBrake) }, { "space", S::concreteRoom }, { "spaceMix", 0.25f }, { "humanize", 0.3f } } },
            { "Pipes", "Lengths of scaffold pipe, ringing long",
              { { "source", (float) sourceIndex (sPipe) }, { "space", S::carPark }, { "spaceMix", 0.3f } } },
            { "Bell Plates", "Struck steel plates: churchy, shimmering and inharmonic",
              { { "source", (float) sourceIndex (sBellPlate) }, { "space", S::church }, { "spaceMix", 0.3f } } },
            { "Glass Bottles", "Tapped bottles, small and bright",
              { { "source", (float) sourceIndex (sGlass) }, { "space", S::livingRoom }, { "spaceMix", 0.2f } } },
            { "Double Bass Pizzicato", "A plucked double bass: dark, woody and round",
              { { "source", (float) sourceIndex (sBassPizz) }, { "space", S::woodenStudio }, { "spaceMix", 0.15f }, { "humanize", 0.15f } } },
            { "Cello Col Legno", "Strings struck with the wood of the bow: skeletal and clicky",
              { { "source", (float) sourceIndex (sColLegno) }, { "space", S::bigLiveRoom }, { "spaceMix", 0.25f }, { "humanize", 0.35f } } },
            { "Bowed Double Bass (drone)", "A slow bowed double bass: hold the notes",
              { { "source", (float) sourceIndex (sBowedBass) }, { "space", S::church }, { "spaceMix", 0.3f }, { "humanize", 0.1f } } },
            { "Bowed Cello", "A bowed cello with a slow vibrato",
              { { "source", (float) sourceIndex (sBowedCello) }, { "space", S::concertHall }, { "spaceMix", 0.25f }, { "humanize", 0.1f } } },
            { "Bowed Saw (ghostly)", "A musical saw: the ghost in the attic",
              { { "source", (float) sourceIndex (sBowedSaw) }, { "space", S::cave }, { "spaceMix", 0.4f }, { "humanize", 0.0f } } },
            { "Bowed Metal (horror)", "A bowed cymbal / steel plate: groaning, shimmering dread",
              { { "source", (float) sourceIndex (sBowedMetal) }, { "space", S::stairwell }, { "spaceMix", 0.45f } } },
            { "Waterphone Drift", "Wobbling, bending metal rods: underwater and unsettling",
              { { "source", (float) sourceIndex (sWaterphone) }, { "space", S::cave }, { "spaceMix", 0.45f } } },
            { "Wind (play the pitch)", "Wind you can play: low keys moan, high keys whistle",
              { { "source", (float) sourceIndex (sWind) }, { "space", S::forest }, { "spaceMix", 0.3f } } },
            { "Deep Gongs", "Huge, slow metal: temple doors and the end of the level",
              { { "source", (float) sourceIndex (sGong) }, { "space", S::cathedral }, { "spaceMix", 0.35f } } },
        };
        return p;
    }
}

juce::StringArray Junkyard::presetNames()
{
    juce::StringArray n;
    for (auto& p : junkPresets()) n.add (p.name);
    return n;
}

juce::String Junkyard::presetDescription (int i)
{
    return juce::isPositiveAndBelow (i, (int) junkPresets().size()) ? juce::String (junkPresets()[(size_t) i].description) : juce::String();
}

void Junkyard::loadProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) junkPresets().size())) return;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : junkPresets()[(size_t) index].values)
        setParam (id, v);
}

// =========================================================================================================
//  Processor
// =========================================================================================================
Junkyard::Junkyard() : BuiltinProcessor ("junkyard", "Junkyard Percussion", true, junkLayout())
{
    for (int i = 0; i < maxVoices; ++i) voices.push_back (std::make_unique<Voice>());
    soundGains();
    loadProgram (0);
    startTimerHz (8);
}

Junkyard::~Junkyard()
{
    stopTimer();
}

void Junkyard::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = juce::jmax (16, block);
    for (auto& v : voices) { v->prepare (sr); v->active = false; }
    tiltLp[0] = tiltLp[1] = 0.0f;
    wet.setSize (2, maxBlock);
    convolution.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    roomKey[0] = roomKey[1] = -1;
    rebuildRoomIfNeeded (true);
}

void Junkyard::timerCallback() { rebuildRoomIfNeeded (false); }

void Junkyard::rebuildRoomIfNeeded (bool force)
{
    const int key[2] = { (int) param ("space"), juce::roundToInt (param ("size") * 100.0f) };
    if (! force && key[0] == roomKey[0] && key[1] == roomKey[1]) return;
    roomKey[0] = key[0]; roomKey[1] = key[1];
    auto ir = roomir::design (key[0], sampleRate, (float) key[1] / 100.0f, 0.5f, 4.0f);
    convolution.loadImpulseResponse (std::move (ir), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    roomReady = true;
}

void Junkyard::waitUntilReady()
{
    rebuildRoomIfNeeded (true);
    juce::Thread::sleep (200);
}

void Junkyard::startNote (int note, float velocity)
{
    const int source = (int) param ("source");
    int snd = -1;
    float hz = 440.0f, voiceGain = 1.0f, pan = 0.0f;
    const float tune = param ("tune");
    if (source <= 0)
    {
        const auto& k = kitMap()[(size_t) juce::jlimit (0, 127, note)];
        if (k.sound < 0) return;
        snd = k.sound;
        hz = sounds()[(size_t) snd].hz * std::pow (2.0f, (k.semis + tune) / 12.0f);
        voiceGain = k.gain;
        pan = k.pan;
    }
    else
    {
        const auto& c = chromaticSounds();
        snd = c[(size_t) juce::jlimit (0, (int) c.size() - 1, source - 1)];
        hz = mtof ((float) note + tune);
        // keep the keyboard's spread gentle for one instrument
        pan = juce::jlimit (-0.5f, 0.5f, ((float) note - 60.0f) / 48.0f);
    }
    voiceGain *= soundGains()[(size_t) snd];

    // no more than three of the same key at once (fast loops): fade the oldest
    int same = 0;
    Voice* oldestSame = nullptr;
    for (auto& v : voices)
        if (v->active && ! v->fading && v->note == note)
        {
            ++same;
            if (oldestSame == nullptr || v->order < oldestSame->order) oldestSame = v.get();
        }
    if (same >= 3 && oldestSame != nullptr) oldestSame->fading = true;

    Voice* target = nullptr;
    for (auto& v : voices) if (! v->active) { target = v.get(); break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (target == nullptr || (! v->held && target->held) || (v->held == target->held && v->order < target->order)) target = v.get();

    Globals gl;
    gl.decay = param ("decay");
    gl.humanize = param ("humanize");
    gl.rattle = param ("rattle");
    gl.width = param ("width");
    target->start (note, snd, hz, velocity, voiceGain, pan, gl, ++noteCounter + (juce::uint32) random.nextInt());
    target->order = noteCounter;
}

void Junkyard::stopNote (int note)
{
    for (auto& v : voices)
        if (v->active && v->held && v->note == note) v->release();
}

void Junkyard::allNotesOff()
{
    for (auto& v : voices) if (v->active) { v->release(); v->fading = true; }
}

void Junkyard::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int total = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int n = juce::jmin (end - pos, 64);
            for (auto& v : voices)
                if (v->active)
                {
                    const float pk = v->render (L + pos, R + pos, n);
                    if (v->finished (pk, n)) v->active = false;
                }
            pos += n;
        }
    };
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        renderTo (juce::jlimit (0, total, m.samplePosition));
        if (msg.isNoteOn()) startNote (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) stopNote (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) allNotesOff();
    }
    renderTo (total);

    int count = 0;
    for (auto& v : voices) count += v->active ? 1 : 0;
    activeVoices = count;

    // ---- tone tilt, grit ----
    const float tone = param ("tone");
    const float hiGain = std::pow (2.0f, tone * 1.3f), loGain = std::pow (2.0f, -tone * 0.4f);
    const float a = 1.0f - std::exp (-twoPi * 1200.0f / (float) sampleRate);
    const float drive = param ("drive");
    const float dg = 1.0f + drive * 8.0f, dNorm = 1.0f / std::pow (dg, 0.65f);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        float z = tiltLp[ch];
        for (int i = 0; i < total; ++i)
        {
            z += a * (x[i] - z);
            float y = z * loGain + (x[i] - z) * hiGain;
            if (drive > 0.001f) y = std::tanh (y * dg) * dNorm;
            x[i] = y;
        }
        tiltLp[ch] = z;
    }

    // ---- the space ----
    const float mix = param ("spaceMix");
    if (roomReady.load() && mix > 0.001f && (int) param ("space") != (int) roomir::dry)
    {
        if (wet.getNumSamples() < total) wet.setSize (2, total, false, false, true);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, 0, buffer, ch, 0, total);
        juce::dsp::AudioBlock<float> blk (wet.getArrayOfWritePointers(), 2, (size_t) total);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (blk));
        const float dryG = std::cos (mix * juce::MathConstants<float>::halfPi * 0.85f);
        const float wetG = std::sin (mix * juce::MathConstants<float>::halfPi) * 0.7f;
        for (int ch = 0; ch < 2; ++ch)
        {
            buffer.applyGain (ch, 0, total, dryG);
            buffer.addFrom (ch, 0, wet, ch, 0, total, wetG);
        }
    }

    // ---- volume, safety ----
    const float vol = juce::Decibels::decibelsToGain (param ("volume"));
    for (int ch = 0; ch < 2; ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        for (int i = 0; i < total; ++i)
        {
            const float y = x[i] * vol;
            x[i] = std::abs (y) < 0.9f ? y : std::tanh (y);
        }
    }
}

} // namespace wis::daw
