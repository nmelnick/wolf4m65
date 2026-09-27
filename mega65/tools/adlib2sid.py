#!/usr/bin/env python3
"""adlib2sid: the game's AdLib sound effects (item pickups, weapon select,
"no way", chimes...) converted for a SID, for the MEGA65 build to play on
its fourth SID ($D460; the music has the other three).

usage: adlib2sid.py AUDIOHED AUDIOT OUT.DAT [--preview FILE.sid] [--model 6581|8580]

An AdLib effect (AUDIOT chunk LASTSOUND + sound) is an instrument (a 16-byte
Instrument, on channel 0: modulator cell 0, carrier cell 3) and a string of
notes, one per 140Hz tick: a non-zero byte is channel 0's frequency (the
low 8 bits; the high 2 bits are 0) keyed on with the sound's block, zero
keys it off (Wolf4SDL's SDL_ALPlaySound and its player). The instrument
becomes a SID patch (waveform, envelope, level) with the same mapping as
the AdLib music (imf2sid.channel_voice), and each note a SID frequency.

OUT.DAT (m65_sd.cpp, m65_sidfx.s): "WSFX", version 2, sound count (8 bits),
tick rate (16 bits); then per sound (13 bytes): file offset (32 bits, 0:
none), ticks (16 bits), priority (16 bits), control (waveform), attack/
decay, sustain/release, pulse width (16 bits); then per sound its ticks: a
SID frequency (16 bits) each, 0 for key off. Built from the user's own game
data: not for passing on.

--preview writes a PSID that plays every effect in turn (0.4s apart), to
listen to them with VICE's vsid.
"""
import argparse
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imf2sid import Voice, channel_voice                 # noqa: E402
from sidpack import build_player, encode, psid          # noqa: E402

NSOUNDS = 87                    # LASTSOUND (audiowl6.h); AdLib effects follow
TICKRATE = 140
PULSE = 0x800


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


def write_dat(path, fx):
    head = b"WSFX" + struct.pack("<BBH", 2, NSOUNDS, TICKRATE)
    table, body = b"", b""
    base = len(head) + NSOUNDS * 13
    for s in range(NSOUNDS):
        if s in fx:
            prio, patch, notes = fx[s]
            table += struct.pack("<IHHBBBH", base + len(body), len(notes), prio,
                                 patch.wave, patch.ad, patch.sr, PULSE)
            body += struct.pack(f"<{len(notes)}H", *notes)
        else:
            table += bytes(13)
    open(path, "wb").write(head + table + body)
    print(f"{path}: {len(fx)} AdLib effects, {len(body)} bytes of notes")


def write_preview(path, fx, model):
    """A PSID: every effect in turn on voice 1, as the game plays them."""
    records = [([(0x17, 0), (0x18, 0x0F), (2, PULSE & 0xFF), (3, PULSE >> 8)], 0)]
    for s in sorted(fx):
        prio, patch, notes = fx[s]
        records.append(([(5, patch.ad), (6, patch.sr), (4, patch.wave)], 1))
        gate, last = 0, None
        for f in notes:
            w = []
            if f:
                if f != last:
                    w = [(0, f & 0xFF), (1, f >> 8)]
                if not gate:
                    w.append((4, patch.wave | 1))
                gate, last = 1, f
            elif gate:
                w = [(4, patch.wave)]
                gate = 0
            if w:
                records.append((w, 1))
            else:                       # nothing new: the last record waits longer
                ws, d = records[-1]
                records[-1] = (ws, d + 1)
        records.append(([(4, patch.wave)], TICKRATE * 2 // 5))
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
    args = ap.parse_args()
    fx = effects(open(args.audiohed, "rb").read(), open(args.audiot, "rb").read())
    write_dat(args.out, fx)
    if args.preview:
        write_preview(args.preview, fx, args.model)


if __name__ == "__main__":
    main()
