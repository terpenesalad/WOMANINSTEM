#include "SongAnalysis.h"
#include "Daw/Engine/AudioCache.h"
#include <set>
#include <array>
#include <cmath>

namespace wis::daw
{

// ---- snapshot ---------------------------------------------------------------------------------------------
SongSnapshot SongSnapshot::capture (const Project& p, int excludeTrackId)
{
    SongSnapshot s;
    s.tempo = p.tempo();
    s.beatsPerBar = p.beatsPerBar();
    s.tsNum = p.tsNum();
    s.tsDen = p.tsDen();
    int trackIndex = 0;
    for (auto tv : p.tracks())
    {
        Track t (tv);
        ++trackIndex;
        if (t.isBus() || t.id() == excludeTrackId) continue;
        if (t.isInstrument() && t.instrument()[ids::uid].toString() == "drummer") continue;   // other Smart Drummers
        bool used = false;
        for (auto cv : t.clips())
        {
            Clip c (cv);
            if (c.isMidi())
            {
                const double len = c.midiLength();
                for (auto n : c.v)
                {
                    if (! n.hasType (ids::NOTE)) continue;
                    const double sb = n[ids::s];
                    if (sb < 0.0 || sb >= len) continue;
                    s.notes.push_back ({ c.start() + sb, (float) (int) n[ids::v] / 127.0f, (int) n[ids::p], trackIndex });
                    used = true;
                }
                s.endBeat = juce::jmax (s.endBeat, c.start() + len);
            }
            else
            {
                AudioPart a;
                a.file = p.resolve (c.file());
                a.startBeat = c.start();
                a.offsetSeconds = c.offsetSeconds();
                a.lengthSeconds = c.lengthSeconds();
                a.stretchRatio = c.stretchRatio (s.tempo);
                a.reversed = c.reversed();
                a.gain = juce::Decibels::decibelsToGain (c.gainDb());
                a.track = trackIndex;
                if (a.lengthSeconds > 0.0) { s.audio.push_back (a); used = true; }
                s.endBeat = juce::jmax (s.endBeat, c.endBeats (s.tempo));
            }
        }
        if (used) s.trackNames.add (t.name());
    }
    for (auto m : p.markers())
        s.markers.push_back ({ (double) m[ids::b], m[ids::name].toString() });
    std::sort (s.markers.begin(), s.markers.end(), [] (const Marker& a, const Marker& b) { return a.beat < b.beat; });
    return s;
}

juce::String SongMap::summary() const
{
    if (isEmpty()) return "Nothing to play along to yet: the drummer will play the style on its own.";
    juce::String s;
    s << juce::String (tempo, 0) << " BPM, " << tsNum << "/" << tsDen << ", " << (lastActiveBar - firstActiveBar + 1) << " bars";
    s << (tripletFeel ? ", shuffle feel" : swing > 0.25f ? ", swung (" + juce::String (juce::roundToInt (swing * 100.0f)) + "%)" : ", straight");
    s << ", " << (int) sections.size() << (sections.size() == 1 ? " section" : " sections");
    return s;
}

namespace
{
    float percentile (std::vector<float> v, float q)
    {
        v.erase (std::remove_if (v.begin(), v.end(), [] (float x) { return x <= 0.0f; }), v.end());
        if (v.empty()) return 0.0f;
        const size_t k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::floor (q * (float) (v.size() - 1)));
        std::nth_element (v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    }
}

// ---- analysis ---------------------------------------------------------------------------------------------
SongMap analyzeSong (const SongSnapshot& s, AudioCache* cache, double sampleRate)
{
    SongMap m;
    m.tempo = s.tempo;
    m.beatsPerBar = s.beatsPerBar;
    m.tsNum = s.tsNum;
    m.tsDen = s.tsDen;
    m.stepsPerBar = juce::jmax (1, juce::roundToInt (s.beatsPerBar * 4.0));
    m.bars = juce::jlimit (0, 2000, (int) std::ceil (s.endBeat / s.beatsPerBar - 1.0e-6));
    const int bars = m.bars, spb = m.stepsPerBar;
    m.bar.resize ((size_t) bars);
    for (auto& b : m.bar) { b.accents.assign ((size_t) spb, 0.0f); b.low.assign ((size_t) spb, 0.0f); }
    if (bars == 0) return m;

    std::vector<std::vector<float>> midiAcc ((size_t) bars, std::vector<float> ((size_t) spb, 0.0f)), midiLow = midiAcc, audAcc = midiAcc, audLow = midiAcc;
    std::vector<float> midiEnergy ((size_t) bars, 0.0f), audioEnergy ((size_t) bars, 0.0f);
    std::vector<std::vector<int>> active ((size_t) bars);
    std::array<double, 24> hist {};
    auto markActive = [&] (int bar, int track)
    {
        auto& a = active[(size_t) bar];
        if (std::find (a.begin(), a.end(), track) == a.end()) a.push_back (track);
    };
    auto locate = [&] (double beat, int& bar, int& step)
    {
        bar = (int) std::floor (beat / s.beatsPerBar);
        step = juce::roundToInt ((beat - bar * s.beatsPerBar) * 4.0);
        if (step >= spb) { ++bar; step = 0; }
        return juce::isPositiveAndBelow (bar, bars);
    };

    // ---- MIDI notes ----
    for (auto& n : s.notes)
    {
        int bar, step;
        if (! locate (n.beat, bar, step)) continue;
        midiAcc[(size_t) bar][(size_t) step] += n.velocity;
        if (n.pitch < 53) midiLow[(size_t) bar][(size_t) step] += n.velocity;
        midiEnergy[(size_t) bar] += n.velocity * n.velocity;
        markActive (bar, n.track);
        const double frac = n.beat - std::floor (n.beat);
        hist[(size_t) (juce::roundToInt (frac * 24.0) % 24)] += n.velocity;
    }

    // ---- audio: onset strength (positive change in log energy) in a full band and a low band ----
    const double spbSec = 60.0 / s.tempo;
    for (auto& part : s.audio)
    {
        if (cache == nullptr) break;
        auto data = cache->getBlocking (part.file, sampleRate);
        if (data == nullptr || data->buffer.getNumSamples() == 0) continue;
        ++m.partsAnalysed;
        const double sr = data->sampleRate;
        const int hop = juce::jmax (64, (int) (sr / 200.0));
        const int chans = data->buffer.getNumChannels();
        const int i0 = juce::jlimit (0, data->buffer.getNumSamples(), (int) (part.offsetSeconds * sr));
        const int i1 = juce::jlimit (i0, data->buffer.getNumSamples(), (int) ((part.offsetSeconds + part.lengthSeconds) * sr));
        const float aLow = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 150.0f / (float) sr);
        float lp1 = 0.0f, lp2 = 0.0f;
        struct Frame { double beat; float full, low, rms; };
        std::vector<Frame> frames;
        frames.reserve ((size_t) ((i1 - i0) / hop + 2));
        float prevF = -10.0f, prevL = -10.0f;
        for (int i = i0; i + hop <= i1; i += hop)
        {
            double ef = 0.0, el = 0.0;
            for (int k = 0; k < hop; ++k)
            {
                float x = 0.0f;
                for (int c = 0; c < chans; ++c) x += data->buffer.getSample (c, i + k);
                x /= (float) chans;
                lp1 += aLow * (x - lp1);
                lp2 += aLow * (lp1 - lp2);
                ef += x * x;
                el += lp2 * lp2;
            }
            ef /= hop; el /= hop;
            const float lf = (float) std::log10 (ef + 1.0e-10), ll = (float) std::log10 (el + 1.0e-10);
            const double srcSec = (i + hop * 0.5) / sr;
            const double t = (part.reversed ? (part.offsetSeconds + part.lengthSeconds - srcSec) : (srcSec - part.offsetSeconds)) * part.stretchRatio;
            frames.push_back ({ part.startBeat + t / spbSec, juce::jmax (0.0f, lf - prevF), juce::jmax (0.0f, ll - prevL), (float) std::sqrt (ef) * part.gain });
            prevF = lf; prevL = ll;
        }
        if (frames.size() < 3) continue;
        // each part is judged against itself, so a quiet stem counts as much as a loud one
        std::vector<float> fl, ll;
        for (auto& f : frames) { if (f.rms > 1.0e-4f) { fl.push_back (f.full); ll.push_back (f.low); } }
        const float nf = juce::jmax (0.05f, percentile (fl, 0.97f)), nl = juce::jmax (0.05f, percentile (ll, 0.97f));
        std::vector<double> barRms ((size_t) bars, 0.0);
        std::vector<int> barFrames ((size_t) bars, 0);
        for (auto& f : frames)
        {
            int bar, step;
            if (! locate (f.beat, bar, step)) continue;
            const float full = juce::jmin (1.5f, f.full / nf), low = juce::jmin (1.5f, f.low / nl);
            auto& a = audAcc[(size_t) bar][(size_t) step];
            a = juce::jmax (a, f.rms > 1.0e-4f ? full : 0.0f);
            auto& l = audLow[(size_t) bar][(size_t) step];
            l = juce::jmax (l, f.rms > 1.0e-4f ? low : 0.0f);
            barRms[(size_t) bar] += f.rms;
            ++barFrames[(size_t) bar];
            if (f.rms > 0.003f) markActive (bar, part.track);
            if (full > 0.5f)
            {
                const double frac = f.beat - std::floor (f.beat);
                hist[(size_t) (juce::roundToInt (frac * 24.0) % 24)] += full;
            }
        }
        for (int b = 0; b < bars; ++b)
            if (barFrames[(size_t) b] > 0) audioEnergy[(size_t) b] += (float) (barRms[(size_t) b] / barFrames[(size_t) b]);
    }

    // ---- combine ----
    auto normalise = [&] (std::vector<std::vector<float>>& grid)
    {
        std::vector<float> all;
        for (auto& b : grid) all.insert (all.end(), b.begin(), b.end());
        const float p = percentile (all, 0.95f);
        if (p <= 0.0f) return false;
        for (auto& b : grid) for (auto& x : b) x = juce::jmin (1.0f, x / p);
        return true;
    };
    const bool hasMidi = normalise (midiAcc);
    normalise (midiLow);
    const bool hasAudio = normalise (audAcc);
    normalise (audLow);
    const float maxMidiE = *std::max_element (midiEnergy.begin(), midiEnergy.end());
    const float maxAudE = *std::max_element (audioEnergy.begin(), audioEnergy.end());
    size_t maxActive = 1;
    for (auto& a : active) maxActive = juce::jmax (maxActive, a.size());
    float maxE = 0.0f;
    for (int b = 0; b < bars; ++b)
    {
        auto& bb = m.bar[(size_t) b];
        int onsets = 0;
        for (int st = 0; st < spb; ++st)
        {
            const float a = hasMidi && hasAudio ? juce::jmax (midiAcc[(size_t) b][(size_t) st], audAcc[(size_t) b][(size_t) st]) * 0.7f
                                                    + 0.3f * 0.5f * (midiAcc[(size_t) b][(size_t) st] + audAcc[(size_t) b][(size_t) st])
                                                : (hasMidi ? midiAcc[(size_t) b][(size_t) st] : audAcc[(size_t) b][(size_t) st]);
            bb.accents[(size_t) st] = juce::jlimit (0.0f, 1.0f, a);
            bb.low[(size_t) st] = juce::jlimit (0.0f, 1.0f, juce::jmax (midiLow[(size_t) b][(size_t) st], audLow[(size_t) b][(size_t) st]));
            if (bb.accents[(size_t) st] > 0.25f) ++onsets;
        }
        bb.density = (float) onsets / (float) spb;
        const float me = maxMidiE > 0.0f ? std::sqrt (midiEnergy[(size_t) b] / maxMidiE) : 0.0f;
        const float ae = maxAudE > 0.0f ? audioEnergy[(size_t) b] / maxAudE : 0.0f;
        const float level = maxMidiE > 0.0f && maxAudE > 0.0f ? 0.6f * ae + 0.4f * me : juce::jmax (me, ae);
        bb.energy = 0.7f * level + 0.3f * (float) active[(size_t) b].size() / (float) maxActive;
        maxE = juce::jmax (maxE, bb.energy);
    }
    for (int b = 0; b < bars; ++b)
    {
        auto& bb = m.bar[(size_t) b];
        if (maxE > 0.0f) bb.energy /= maxE;
        bb.active = ! active[(size_t) b].empty() && (bb.energy > 0.03f || bb.density > 0.0f);
    }
    m.firstActiveBar = bars;
    m.lastActiveBar = -1;
    for (int b = 0; b < bars; ++b)
        if (m.bar[(size_t) b].active) { m.firstActiveBar = juce::jmin (m.firstActiveBar, b); m.lastActiveBar = b; }

    // ---- swing: onsets just after the half-beat (straight) or at two thirds (swung) ----
    {
        const double straight = hist[12], swung = hist[16] + 0.5 * (hist[15] + hist[17]);
        const double total = straight + swung;
        double sw = total > 1.0e-6 ? (swung - straight) / total : 0.0;
        const double trip = hist[8] + hist[16], duple = hist[6] + hist[12] + hist[18];
        m.tripletFeel = trip > 1.3 * duple && trip > 0.05 * (hist[0] + 1.0);
        m.swing = (float) juce::jlimit (0.0, 1.0, sw);
        if (m.tripletFeel) m.swing = juce::jmax (m.swing, 0.8f);
    }

    findSections (m, s.markers, active);
    return m;
}

// ---- sections ---------------------------------------------------------------------------------------------
void findSections (SongMap& m, const std::vector<SongSnapshot::Marker>& markers, const std::vector<std::vector<int>>& activeTracks)
{
    m.sections.clear();
    if (m.bars == 0) return;
    const int first = juce::jlimit (0, m.bars - 1, m.firstActiveBar < m.bars ? m.firstActiveBar : 0);
    const int last = m.lastActiveBar >= first ? m.lastActiveBar : m.bars - 1;

    std::vector<int> bounds;
    std::vector<juce::String> names;
    if (! markers.empty())
    {
        // the song's own markers
        bounds.push_back (first);
        names.push_back ({});
        for (auto& mk : markers)
        {
            const int b = juce::jlimit (0, m.bars, juce::roundToInt (mk.beat / m.beatsPerBar));
            if (b <= first)
            {
                names.front() = mk.name;   // a marker at (or before) the start names the first section
                continue;
            }
            if (b > last) continue;
            if (b != bounds.back()) { bounds.push_back (b); names.push_back (mk.name); }
            else names.back() = mk.name;
        }
    }
    else
    {
        // where the music changes: energy, busyness or who's playing, judged 4 bars either side, on 4-bar lines
        auto window = [&] (int from, int to, float& e, float& d, std::set<int>& who)
        {
            e = d = 0.0f;
            int n = 0;
            for (int b = juce::jmax (first, from); b < juce::jmin (last + 1, to); ++b, ++n)
            {
                e += m.bar[(size_t) b].energy;
                d += m.bar[(size_t) b].density;
                for (int t : activeTracks[(size_t) b]) who.insert (t);
            }
            if (n > 0) { e /= (float) n; d /= (float) n; }
            return n;
        };
        bounds.push_back (first);
        names.push_back ({});
        std::vector<std::pair<int, float>> novelty;
        for (int b = first + 4; b <= last - 3; b += 4)
        {
            float e1, d1, e2, d2;
            std::set<int> w1, w2;
            if (window (b - 4, b, e1, d1, w1) < 2 || window (b, b + 4, e2, d2, w2) < 2) continue;
            int diff = 0;
            for (int t : w1) if (w2.count (t) == 0) ++diff;
            for (int t : w2) if (w1.count (t) == 0) ++diff;
            const float nov = std::abs (e2 - e1) + 0.5f * std::abs (d2 - d1) + 0.12f * (float) diff;
            novelty.push_back ({ b, nov });
        }
        for (size_t i = 0; i < novelty.size(); ++i)
        {
            const float v = novelty[i].second;
            const bool peak = (i == 0 || v >= novelty[i - 1].second) && (i + 1 == novelty.size() || v >= novelty[i + 1].second);
            if (v > 0.16f && peak && novelty[i].first - bounds.back() >= 4) { bounds.push_back (novelty[i].first); names.push_back ({}); }
        }
        // very long stretches get split into 8-bar phrases anyway (so every 8 bars can lift a little)
        std::vector<int> fine;
        std::vector<juce::String> fineNames;
        for (size_t i = 0; i < bounds.size(); ++i)
        {
            const int end = i + 1 < bounds.size() ? bounds[i + 1] : last + 1;
            fine.push_back (bounds[i]); fineNames.push_back (names[i]);
            if (end - bounds[i] > 24)
                for (int b = bounds[i] + 16; b < end - 4; b += 16) { fine.push_back (b); fineNames.push_back ({}); }
        }
        bounds = fine;
        names = fineNames;
    }

    for (size_t i = 0; i < bounds.size(); ++i)
    {
        SongMap::Section sec;
        sec.startBar = bounds[i];
        const int end = i + 1 < bounds.size() ? bounds[i + 1] : last + 1;
        sec.bars = juce::jmax (1, end - sec.startBar);
        sec.name = names[i];
        int act = 0;
        for (int b = sec.startBar; b < sec.startBar + sec.bars && b < m.bars; ++b)
        {
            sec.energy += m.bar[(size_t) b].energy;
            if (m.bar[(size_t) b].active) ++act;
        }
        sec.energy /= (float) sec.bars;
        sec.silent = act == 0;
        m.sections.push_back (sec);
    }

    // how hard to play: relative to the loudest section
    float maxE = 0.0f, minE = 1.0f;
    for (auto& s : m.sections) if (! s.silent) { maxE = juce::jmax (maxE, s.energy); minE = juce::jmin (minE, s.energy); }
    const float range = juce::jmax (0.08f, maxE - minE);
    int verses = 0, choruses = 0;
    for (size_t i = 0; i < m.sections.size(); ++i)
    {
        auto& s = m.sections[i];
        const float rel = s.silent ? 0.0f : (s.energy - minE) / range;
        // quiet (only when it's really quiet), verse or chorus; the peak is kept for the song's last big chorus
        s.intensity = s.silent ? 0 : (rel < 0.35f ? (s.energy < 0.2f && maxE > 0.5f ? 0 : 1) : 2);
        if (m.sections.size() == 1) s.intensity = 2;
        if (s.name.isNotEmpty())
        {
            // markers named like song parts set the intensity too
            const auto n = s.name.toLowerCase();
            if (n.contains ("intro") || n.contains ("outro") || n.contains ("break")) s.intensity = juce::jmin (s.intensity, 1);
            else if (n.contains ("chorus") || n.contains ("drop") || n.contains ("hook")) s.intensity = juce::jmax (s.intensity, 2);
            else if (n.contains ("verse")) s.intensity = juce::jlimit (1, 2, s.intensity);
            else if (n.contains ("bridge")) s.intensity = juce::jlimit (1, 2, s.intensity);
            continue;
        }
        if (s.silent) { s.name = "Break"; continue; }
        const bool isFirst = i == 0, isLast = i + 1 == m.sections.size();
        if (isFirst && m.sections.size() > 2 && s.bars <= 8 && s.intensity <= 1) { s.name = "Intro"; continue; }
        if (isLast && m.sections.size() > 2 && s.bars <= 8 && s.intensity <= 1) { s.name = "Outro"; continue; }
        if (s.intensity >= 2 && rel > 0.6f) s.name = "Chorus " + juce::String (++choruses);
        else s.name = "Verse " + juce::String (++verses);
    }
    // the climax: the last full-strength section of a song with at least four sections
    if (m.sections.size() >= 4)
        for (int i = (int) m.sections.size(); --i >= 0;)
            if (m.sections[(size_t) i].intensity == 2 && m.sections[(size_t) i].name.isEmpty() == false && ! m.sections[(size_t) i].silent)
            {
                if (m.sections[(size_t) i].energy >= maxE * 0.95f) m.sections[(size_t) i].intensity = 3;
                break;
            }
    if (choruses == 1) for (auto& s : m.sections) if (s.name == "Chorus 1") s.name = "Chorus";
    if (verses == 1) for (auto& s : m.sections) if (s.name == "Verse 1") s.name = "Verse";
}

} // namespace wis::daw
