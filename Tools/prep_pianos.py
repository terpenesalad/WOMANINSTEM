#!/usr/bin/env python3
"""Builds WOMANINSTEM's piano sample packs from three freely licensed piano libraries.

  grand   Salamander Grand Piano V3 (Yamaha C5, Alexander Holm, CC-BY 3.0)       6 of its 16 velocity layers
  steinway Splendid Grand Piano (Steinway samples released to the public domain by Akai) 4 layers
  upright Upright Piano KW (Kawai upright, FreePats, CC0)                          2 layers

Each sample: leading silence trimmed, cut once it has decayed ~60 dB (or at a length that shrinks up the
keyboard), short fade, stored as 16-bit stereo FLAC. pack.json lists key centres and velocity layers.
usage: prep_pianos.py <download cache dir> <output dir>      (needs: pip install soundfile numpy)
CI runs this once and caches the result; the packs ship in the release next to the app (pianos/<id>/).
"""
import json, os, re, sys, urllib.request, urllib.parse
import numpy as np
import soundfile as sf

CACHE, OUT = sys.argv[1], sys.argv[2]
RAW = "https://raw.githubusercontent.com/"
NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

def midi_of(name):
    m = re.match(r"([A-G]#?)(-?\d+)$", name)
    return NOTE[m.group(1)] + 12 * (int(m.group(2)) + 1)

def fetch(repo, path):
    local = os.path.join(CACHE, repo.replace("/", "_"), path)
    if not os.path.exists(local):
        os.makedirs(os.path.dirname(local), exist_ok=True)
        url = RAW + repo + "/HEAD/" + urllib.parse.quote(path)
        for attempt in range(4):
            try:
                urllib.request.urlretrieve(url, local + ".part")
                os.replace(local + ".part", local)
                break
            except Exception as e:
                if attempt == 3: raise
                print("  retry", path, e, flush=True)
    return local

def process(src, key, dst):
    x, sr = sf.read(src, dtype="float32", always_2d=True)
    if x.shape[1] == 1: x = np.repeat(x, 2, axis=1)
    x = x[:, :2]
    mono = np.abs(x).max(axis=1)
    peak = float(mono.max()) if len(mono) else 0.0
    if peak <= 0: return None
    # onset: first sample above -40 dB of the peak, keep 2 ms before it
    on = int(np.argmax(mono > peak * 0.01))
    start = max(0, on - int(0.002 * sr))
    # end: where a 50 ms RMS window falls 60 dB under the loudest window, capped by key
    hop = int(0.05 * sr)
    n = (len(x) - start) // hop
    rms = np.array([np.sqrt(np.mean(x[start + i * hop:start + (i + 1) * hop] ** 2)) for i in range(n)]) if n > 0 else np.array([peak])
    loud = rms.max()
    below = np.where(rms < loud * 10 ** (-60 / 20))[0]
    below = below[below > 4]
    end_t = (below[0] * hop) / sr if len(below) else (len(x) - start) / sr
    cap = 16.0 * (0.5 ** ((key - 21) / 20.0))   # 16 s at A0, halving every 20 semitones (~2.5 s at the top)
    length = int(min(end_t, max(cap, 1.5)) * sr)
    y = x[start:start + length].copy()
    fade = min(len(y) // 4, int(0.3 * sr))
    if fade > 0:
        y[-fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)[:, None] ** 2
    sf.write(dst, y, sr, subtype="PCM_16", format="FLAC")
    return sr, len(y)

def build(pack_id, name, credit, license_, layers, items):
    """items: (repo, path, key, layer)"""
    out_dir = os.path.join(OUT, pack_id)
    os.makedirs(out_dir, exist_ok=True)
    samples, total = [], 0
    for i, (repo, path, key, layer) in enumerate(items):
        fn = f"{key:03d}_{layer}.flac"
        dst = os.path.join(out_dir, fn)
        src = fetch(repo, path)
        r = process(src, key, dst)
        if r is None: continue
        sr, frames = r
        total += frames
        samples.append({"file": fn, "key": key, "layer": layer, "rate": sr, "frames": frames})
        if i % 20 == 0: print(f"  {pack_id}: {i + 1}/{len(items)}", flush=True)
    json.dump({"id": pack_id, "name": name, "credit": credit, "license": license_, "layers": layers, "samples": samples},
              open(os.path.join(out_dir, "pack.json"), "w"), indent=1)
    print(f"{pack_id}: {len(samples)} samples, {total / 48000:.0f} s of audio, ~{total * 4 / 1e6:.0f} MB in memory", flush=True)

# ---- Salamander: notes every minor third from A0 (A, C, D#, F#), layers v1 v4 v7 v10 v13 v16 ----
sal_layers = [1, 4, 7, 10, 13, 16]
sal_items = []
for octave in range(0, 9):
    for nm in ["A", "C", "D#", "F#"]:
        key = midi_of(f"{nm}{octave}")
        if key < 21 or key > 108: continue
        for li, v in enumerate(sal_layers):
            sal_items.append(("sfzinstruments/SalamanderGrandPiano", f"Samples/{nm}{octave}v{v}.flac", key, li))
build("grand", "Concert Grand", "Salamander Grand Piano V3 by Alexander Holm (Yamaha C5)", "CC-BY 3.0",
      [{"lo": 1, "hi": 30}, {"lo": 31, "hi": 50}, {"lo": 51, "hi": 70}, {"lo": 71, "hi": 92}, {"lo": 93, "hi": 112}, {"lo": 113, "hi": 127}],
      sal_items)

# ---- Splendid Grand: 4 layers, mapping from the SFZ data files ----
spl_items = []
for li, f in enumerate(["pp", "mp", "mf", "ff"]):
    for line in open(fetch("sfzinstruments/SplendidGrandPiano", f"Data/{f.upper()}.txt")):
        m = re.search(r"pitch_keycenter=(\d+).*sample=(.+)\.\$EXT", line)
        if m: spl_items.append(("sfzinstruments/SplendidGrandPiano", "Samples/" + m.group(2).strip() + ".flac", int(m.group(1)), li))
build("steinway", "Vintage Grand", "Splendid Grand Piano (Steinway samples released to the public domain by Akai; SFZ by kinwie)", "Public domain",
      [{"lo": 1, "hi": 40}, {"lo": 41, "hi": 67}, {"lo": 68, "hi": 84}, {"lo": 85, "hi": 127}], spl_items)

# ---- Upright KW: 2 layers ----
kw_items = []
sfz = open(fetch("FreePats/upright-piano-kw", "UprightPianoKW-20220221.sfz")).read()
for m in re.finditer(r"pitch_keycenter=(\d+)[\s\S]*?sample=samples/(\S+)\.flac", sfz):
    key, nm = int(m.group(1)), m.group(2)
    kw_items.append(("FreePats/upright-piano-kw", f"samples/{nm}.flac", key, 1 if nm.endswith("vH") else 0))
build("upright", "Upright", "Upright Piano KW by Gonzalo and Roberto, FreePats project (Kawai upright)", "CC0",
      [{"lo": 1, "hi": 80}, {"lo": 81, "hi": 127}], kw_items)

open(os.path.join(OUT, "CREDITS.txt"), "w").write("""WOMANINSTEM piano sample packs (trimmed and re-encoded for the Piano Room instrument by Tools/prep_pianos.py)

grand/     Salamander Grand Piano V3 by Alexander Holm (Yamaha C5). CC-BY 3.0, https://creativecommons.org/licenses/by/3.0/
           Source: https://archive.org/details/SalamanderGrandPianoV3 (via https://github.com/sfzinstruments/SalamanderGrandPiano)
           6 of the 16 velocity layers, trimmed.
steinway/  Splendid Grand Piano: Steinway samples released into the public domain by Akai; SFZ mapping by kinwie
           (https://github.com/sfzinstruments/SplendidGrandPiano). Trimmed.
upright/   Upright Piano KW by Gonzalo and Roberto, FreePats project (Kawai upright). CC0 public domain dedication.
           http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html#UprightKW  Trimmed.
""")
