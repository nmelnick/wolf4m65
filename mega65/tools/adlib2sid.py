#!/usr/bin/env python3
"""adlib2sid: the game's AdLib sound effects (item pickups, weapon select,
"no way", chimes...) converted for a SID, for the MEGA65 build to play on
its fourth SID ($D460; the music has the other three).

usage: adlib2sid.py AUDIOHED AUDIOT OUT.DAT [--opl host/oplfx] [--preview FILE.sid]
                    [--model 6581|8580]

An AdLib effect (AUDIOT chunk LASTSOUND + sound) is an instrument (a 16-byte
Instrument, on channel 0: modulator cell 0, carrier cell 3) and a string of
notes, one per 140Hz tick: a non-zero byte is channel 0's frequency (the
low 8 bits; the high 2 bits are 0) keyed on with the sound's block, zero
keys it off (Wolf4SDL's SDL_ALPlaySound and its player). Each note becomes
a SID frequency. With --opl, the SID patch is fitted to the effect as the
original plays it (see fit_effects); without, it is a guess from the
instrument's registers (imf2sid.channel_voice, as for the AdLib music).

OUT.DAT (m65_sd.cpp, m65_sidfx.s): "WSFX", version 3, sound count (8 bits),
tick rate (16 bits); then per sound (15 bytes): file offset (32 bits, 0:
none), ticks (16 bits), priority (16 bits), control (waveform), attack/
decay, sustain/release, pulse width (16 bits), low-pass cutoff (16 bits,
$FFFF: none); then per sound its ticks: a SID frequency (16
bits) each, 0 for key off. Built from the user's own game data: not for
passing on.

--preview writes a PSID that plays every effect in turn (0.4s apart), to
listen to them with VICE's vsid.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imf2sid import Voice, channel_voice                 # noqa: E402
from sidpack import PAL_CLOCK, build_player, encode, psid  # noqa: E402

NSOUNDS = 87                    # LASTSOUND (audiowl6.h); AdLib effects follow
TICKRATE = 140
PULSE = 0x800
ENTRY = 15                      # (SFX.DAT: bytes per sound)


def effects(hed, au):
    """-> {sound: (priority, patch Voice, [SID frequency or 0 per tick])}"""
    off = struct.unpack(f"<{len(hed) // 4}I", hed)
    out = {}
    for s in range(NSOUNDS):
        c = NSOUNDS + s
        if c + 1 >= len(off):
            break
        a, b = off[c], off[c + 1]
        if b <= a + 24:
            continue                    # (not in this version of the game)
        length, priority = struct.unpack_from("<IH", au, a)
        inst = au[a + 6:a + 22]
        block = au[a + 22]
        data = au[a + 23:a + 23 + length]
        if not length or len(data) < length or not (inst[6] | inst[7]):
            continue
        regs = [0] * 256
        regs[0x20], regs[0x23] = inst[0], inst[1]       # mChar, cChar
        regs[0x40], regs[0x43] = inst[2], inst[3]       # mScale, cScale
        regs[0x60], regs[0x63] = inst[4], inst[5]       # mAttack, cAttack
        regs[0x80], regs[0x83] = inst[6], inst[7]       # mSus, cSus
        regs[0xE0], regs[0xE3] = inst[8], inst[9]       # mWave, cWave
        regs[0xC0] = 0                                  # (SDL_AlSetFXInst: FM, no feedback)
        keyblock = ((block & 7) << 2) | 0x20
        patch, notes = None, []
        for fnum in data:
            if fnum:
                regs[0xA0], regs[0xB0] = fnum, keyblock
                v = Voice()
                channel_voice(regs, 0, v)
                if patch is None:
                    patch = v
                notes.append(max(v.freq, 1))
            else:
                notes.append(0)
        if patch is None:
            continue                    # (all rests)
        out[s] = (priority, patch, notes)
    return out


# ---- fitted to the original -----------------------------------------------------
#
# channel_voice's mapping is a guess from the OPL registers, and for effects
# it went wrong (an OPL "no decay" read as an instant decay, a "no release"
# as 24 seconds of release). So the effects are rendered as the original
# plays them (host/oplfx.cpp: the DOSBox OPL emulator), and each SID version
# fitted to its render: the envelope (an exact model of the SID's), and the
# waveform and low-pass cutoff (by octave bands: the waveform's harmonics
# through the filter, at the effect's pitch; the fourth SID plays nothing
# else, so its filter is each effect's own).

SR = 44100
PERIOD = [9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251]
WAVES = [(0x10, 0x800), (0x20, 0x800), (0x40, 0x800), (0x40, 0x400), (0x40, 0x200)]
CUTOFFS = [None, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024]
HZ_PER_CUTOFF = 7.1             # (8580, reSIDfp: -3 dB at ~7.1 Hz per register step)
BANDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000]


def sid_env(a, d, s, r, gate):
    """The SID envelope (its rate periods, and the exponential steps of decay
    and release), per ms of gate (key on, per ms) -> level 0..1 per ms."""
    out = np.empty(len(gate))
    level, phase, acc, sus = 0, 'R', 0, s * 17
    for i, on in enumerate(gate):
        if on and phase == 'R':
            phase = 'A'
        elif not on and phase != 'R':
            phase = 'R'
        acc += 1000                                         # (cycles in a ms)
        if phase == 'A':
            n = min(acc // PERIOD[a], 255 - level)
            level += n
            acc -= n * PERIOD[a]
            if level >= 255:
                phase = 'D'
        if phase != 'A':
            rate, low_end = (PERIOD[d], sus) if phase == 'D' else (PERIOD[r], 0)
            while level > low_end:
                div, low = (1, 92) if level >= 93 else (2, 53) if level >= 54 else (4, 25) if level >= 26 \
                    else (8, 13) if level >= 14 else (16, 5) if level >= 6 else (30, -1)
                n = min(acc // (rate * div), level - max(low, low_end))
                if n <= 0:
                    break
                level -= n
                acc -= n * rate * div
            if level <= low_end:
                acc = min(acc, rate)
        out[i] = level / 255
    return out


def env_db(level):
    return np.maximum(20 * np.log10(np.maximum(level, 1e-6)), -40)


ATTACK_MS = [2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800]


def fit_adsr(target_db, gate, start, max_attack=11):
    """(A, D, S, R) whose envelope is closest to target_db (dB re its peak),
    over the moments when either is heard (not the silence after, which
    would outweigh the effect itself)."""
    def err(p):
        m = env_db(sid_env(*p, gate))
        heard = (m > -39.9) | (target_db > -39.9)
        return float(np.mean(np.abs(m - target_db)[heard])) if heard.any() else 0.0
    start = (min(start[0], max_attack),) + tuple(start[1:])
    best, be = list(start), err(start)
    for _ in range(4):
        changed = False
        for i, rng in enumerate((range(max_attack + 1), range(13), range(16), range(13))):
            for v in rng:
                p = best[:]
                p[i] = v
                e = err(p)
                if e < be - 1e-6:
                    best, be, changed = p, e, True
        if not changed:
            break
    return tuple(best)


def model(wave_, pw, f0, cutoff):
    """A waveform's harmonics at f0 through the low-pass -> power per band."""
    n = np.arange(1, int(16000 / f0) + 1)
    f = n * f0
    if wave_ == 0x10:
        a = np.where(n % 2 == 1, 8 / np.pi ** 2 / n ** 2, 0)
    elif wave_ == 0x20:
        a = 2 / np.pi / n
    else:
        a = 4 / np.pi * np.abs(np.sin(np.pi * n * pw / 4096)) / n
    if cutoff is not None:
        x = f / (cutoff * HZ_PER_CUTOFF)
        a = a / np.sqrt((1 - x ** 2) ** 2 + (x / 0.707) ** 2)
    p = a ** 2 / 2
    return np.array([p[(f >= c / 1.414) & (f < c * 1.414)].sum() for c in BANDS])


def band_db(p):
    L = 10 * np.log10(np.maximum(p, 1e-12))
    return np.maximum(L - L.max(), -50)


def render_opl(opl, audiohed, audiot, tmp):
    subprocess.run([opl, audiohed, audiot, tmp, str(NSOUNDS)], check=True, stdout=subprocess.DEVNULL)
    out = {}
    for name in os.listdir(tmp):
        if name.startswith('fx') and name.endswith('.wav'):
            with wave.open(os.path.join(tmp, name)) as w:
                out[int(name[2:4])] = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float)
    return out


def fit_effects(fx, renders):
    """fx (from effects) -> {sound: (priority, wave, AD, SR, pulse width,
    cutoff or None, notes)}, fitted to the OPL renders."""
    ms = SR // 1000
    fitted = {}
    for s, (prio, patch, notes) in fx.items():
        x = renders[s]
        f0 = float(np.median([f * PAL_CLOCK / 16777216 for f in notes if f]))
        n = len(notes) * 1000 // TICKRATE + 300
        gate = [bool(notes[i * TICKRATE // 1000]) if i * TICKRATE // 1000 < len(notes) else False for i in range(n)]
        k = min(len(x) // ms, n)
        # The level per ms: RMS over two of the note's periods (at least 10 ms),
        # so that it follows the loudness, not the waveform.
        w = max(10, int(2000 / f0)) * ms
        p = np.convolve(x[:k * ms] ** 2, np.ones(w) / w, 'same')[ms // 2::ms][:k]
        e = np.sqrt(p)
        target = np.pad(np.maximum(20 * np.log10(e / e.max() + 1e-9), -40), (0, n - k), constant_values=-40)
        # An attack done within a quarter of the first stretch of notes: an FM
        # effect grows louder as its timbre changes, which a slower SID attack
        # would only make quiet.
        run = next((i for i, g in enumerate(gate) if not g), len(gate))
        max_a = max([i for i, t in enumerate(ATTACK_MS) if t <= run / 4] or [0])
        adsr = fit_adsr(target, gate, (patch.ad >> 4, patch.ad & 15, patch.sr >> 4, patch.sr & 15), max_a)
        on = np.repeat(np.array(gate[:k]), ms)
        heard = x[:k * ms][on] if on.any() else x[:k * ms]
        spec = np.abs(np.fft.rfft(heard * np.hanning(len(heard)))) ** 2
        fq = np.fft.rfftfreq(len(heard), 1 / SR)
        ref = band_db(np.array([spec[(fq >= c / 1.414) & (fq < c * 1.414)].sum() for c in BANDS]))
        used = ref > -40
        best = None
        for wave_, pw in WAVES:
            for cut in CUTOFFS:
                if cut is not None and cut * HZ_PER_CUTOFF < 1.5 * f0:
                    continue                                # (not the note itself away)
                err = float(np.mean(np.abs(band_db(model(wave_, pw, f0, cut)) - ref)[used]))
                if best is None or err < best[0] - 0.25:            # (simpler wins a near tie)
                    best = (err, wave_, pw, cut)
        _, wave_, pw, cut = best
        a, d, s_, r = adsr
        fitted[s] = (prio, wave_, a << 4 | d, s_ << 4 | r, pw, cut, notes)
    return fitted


def plain_effects(fx):
    """Without the OPL renders: channel_voice's patches, unfiltered."""
    return {s: (prio, patch.wave, patch.ad, patch.sr, PULSE, None, notes) for s, (prio, patch, notes) in fx.items()}


def write_dat(path, fx):
    head = b"WSFX" + struct.pack("<BBH", 3, NSOUNDS, TICKRATE)
    table, body = b"", b""
    base = len(head) + NSOUNDS * ENTRY
    for s in range(NSOUNDS):
        if s in fx:
            prio, wave_, ad, sr, pw, cut, notes = fx[s]
            table += struct.pack("<IHHBBBHH", base + len(body), len(notes), prio, wave_, ad, sr, pw,
                                 0xFFFF if cut is None else cut)
            body += struct.pack(f"<{len(notes)}H", *notes)
        else:
            table += bytes(ENTRY)
    open(path, "wb").write(head + table + body)
    print(f"{path}: {len(fx)} AdLib effects, {len(body)} bytes of notes")


def write_preview(path, fx, model):
    """A PSID: every effect in turn on voice 1, as the game plays them."""
    records = [([(0x17, 0), (0x18, 0x0F)], 0)]
    for s in sorted(fx):
        prio, wave_, ad, sr, pw, cut, notes = fx[s]
        filt = [(0x15, (cut or 0) & 7), (0x16, (cut or 0) >> 3), (0x17, 0 if cut is None else 1),
                (0x18, (0 if cut is None else 0x10) | 0x0F)]
        records.append((filt + [(2, pw & 0xFF), (3, pw >> 8), (5, ad), (6, sr), (4, wave_)], 1))
        gate, last = 0, None
        for f in notes:
            w = []
            if f:
                if f != last:
                    w = [(0, f & 0xFF), (1, f >> 8)]
                if not gate:
                    w.append((4, wave_ | 1))
                gate, last = 1, f
            elif gate:
                w = [(4, wave_)]
                gate = 0
            if w:
                records.append((w, 1))
            else:                       # nothing new: the last record waits longer
                ws, d = records[-1]
                records[-1] = (ws, d + 1)
        records.append(([(4, wave_)], TICKRATE * 2 // 5))
    with tempfile.TemporaryDirectory() as tmp:
        code, init, play = build_player(tmp, TICKRATE)
        open(path, "wb").write(psid("Wolf3D AdLib effects", "id Software (AdLib->SID)",
                                    code, init, play, encode(records), model))
    print(f"{path}: every effect in turn ({len(fx)})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audiohed")
    ap.add_argument("audiot")
    ap.add_argument("out")
    ap.add_argument("--preview")
    ap.add_argument("--model", default="8580", choices=("6581", "8580"))
    ap.add_argument("--opl", help="host/oplfx (built): fit the effects to the original's sound")
    args = ap.parse_args()
    fx = effects(open(args.audiohed, "rb").read(), open(args.audiot, "rb").read())
    if args.opl:
        with tempfile.TemporaryDirectory() as tmp:
            fx = fit_effects(fx, render_opl(args.opl, args.audiohed, args.audiot, tmp))
    else:
        fx = plain_effects(fx)
    write_dat(args.out, fx)
    if args.preview:
        write_preview(args.preview, fx, args.model)


if __name__ == "__main__":
    main()
