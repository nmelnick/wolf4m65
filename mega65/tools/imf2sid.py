#!/usr/bin/env python3
"""imf2sid: Wolfenstein 3-D's AdLib music as 3-SID PSID files, to hear what
the music could sound like on the MEGA65's SIDs (play them with VICE's vsid).

usage: imf2sid.py AUDIOHED AUDIOT OUTDIR [--song NAME] [--model 6581|8580]

The music is IMF: OPL2 register writes, each followed by a delay in 700Hz
ticks. This replays the writes against a model of the OPL2's registers and,
tick by tick, turns each of the 9 melodic channels into one SID voice
(channel c -> SID c // 3, voice c % 3; SIDs at $D400, $D420, $D440):
  - pitch: the channel's F-number and block, times the carrier's multiplier;
  - gate: the channel's key-on bit (a new key-on restarts the envelope);
  - envelope: the carrier operator's attack/decay/sustain/release, converted
    by time (the OPL2's and the SID's rate tables differ);
  - level: the SID has no per-voice volume, so the carrier's total level and
    sustain level become the SID sustain level (attacks are always full);
  - timbre: a guess from the FM settings: strong modulation -> sawtooth,
    some -> pulse, little or additive -> triangle.
Only the SID register changes are stored. A small 6502 player (assembled
with llvm-mos) plays them from a CIA timer at 700Hz, the IMF rate, so the
timing is exact. Each song becomes OUTDIR/NAME.sid (PSID v4, 3 SIDs).
"""
import argparse
import math
import os
import struct
import subprocess
import tempfile

BIN = os.path.expanduser("~/opt/llvm-mos/bin")
PAL_CLOCK = 985248
IMF_RATE = 700
STARTMUSIC = 261                    # 3 * LASTSOUND (audiowl6.h)
SONGS = ("CORNER DUNGEON WARMARCH GETTHEM HEADACHE HITLWLTZ INTROCW3 NAZI_NOR "
         "NAZI_OMI POW SALUTE SEARCHN SUSPENSE VICTORS WONDERIN FUNKYOU ENDLEVEL "
         "GOINGAFT PREGNANT ULTIMATE NAZI_RAP ZEROHOUR TWELFTH ROSTER URAHERO "
         "VICMARCH PACMAN").split()

# OPL2 operator slots of each channel's modulator (the carrier is +3).
MOD_SLOT = (0, 1, 2, 8, 9, 10, 16, 17, 18)
MULT = (0.5, 1, 2, 3, 4, 5, 6, 7, 8, 8, 10, 10, 12, 12, 15, 15)

# OPL2 envelope times in ms (YMF262/YM3812 data sheets, rate 1..15; 0: never):
# attack 0 -> full, decay/release full -> -96dB (a note is inaudible at about
# half of that).
OPL_ATTACK = [None, 2826, 1413, 706, 353, 176, 88, 44, 22, 11, 5.5, 2.8, 1.4, 0.7, 0.35, 0]
OPL_DECAY = [None] + [39280 / 2 ** (r - 1) for r in range(1, 16)]
# SID envelope times in ms (6581 data sheet): attack 0 -> peak; decay/release
# peak -> 0.
SID_ATTACK = [2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000]
SID_DECAY = [6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000]

WAVE_TRI, WAVE_SAW, WAVE_PULSE = 0x10, 0x20, 0x40


def nearest(table, ms):
    """Index of the table entry closest to `ms` (on a log scale)."""
    ms = max(ms, 0.5)
    return min(range(16), key=lambda i: abs(math.log(table[i] / ms)))


class Voice:
    """What one SID voice should be playing."""
    def __init__(self):
        self.freq = 0
        self.gate = 0
        self.wave = WAVE_TRI
        self.ad = 0x09
        self.sr = 0x00
        self.retrigger = False


def channel_voice(regs, c, v):
    """Update Voice v from OPL2 channel c's registers (pitch, timbre, envelope)."""
    mod = MOD_SLOT[c]
    car = mod + 3
    fnum = regs[0xA0 + c] | (regs[0xB0 + c] & 3) << 8
    block = (regs[0xB0 + c] >> 2) & 7
    hz = fnum * 49716 / 2 ** (20 - block) * MULT[regs[0x20 + car] & 15]
    f = round(hz * 16777216 / PAL_CLOCK)
    while f > 0xFFFF:
        f >>= 1                         # (out of the SID's range: an octave down)
    v.freq = f

    conn = regs[0xC0 + c] & 1
    car_tl = regs[0x40 + car] & 63
    mod_tl = regs[0x40 + mod] & 63
    fb = (regs[0xC0 + c] >> 1) & 7
    if conn:                            # additive: both operators heard
        v.wave = WAVE_TRI if (regs[0xE0 + car] & 3) == 0 else WAVE_PULSE
        tl = min(car_tl, mod_tl)
    else:
        depth = (63 - mod_tl) + fb * 2  # how much the modulator colours the carrier
        v.wave = WAVE_SAW if depth > 44 else WAVE_PULSE if depth > 30 else WAVE_TRI
        tl = car_tl

    ar = regs[0x60 + car] >> 4
    dr = regs[0x60 + car] & 15
    sl = regs[0x80 + car] >> 4
    rr = regs[0x80 + car] & 15
    sustained = regs[0x20 + car] & 0x20
    level = 10 ** (-0.75 * tl / 20)
    sus_level = 10 ** (-3 * sl / 20) if sl < 15 else 0
    a = nearest(SID_ATTACK, OPL_ATTACK[ar]) if ar else 15
    # time to fall to the sustain level (decay), and to silence (release)
    d_ms = OPL_DECAY[dr] * min(sl * 3, 48) / 96 if dr else 1e9
    r_ms = OPL_DECAY[rr] / 2 if rr else 1e9
    if sustained:
        s = round(15 * level * sus_level)
        d = nearest(SID_DECAY, d_ms) if d_ms < 1e8 else 0
    else:                               # no sustain: decays, then releases, while held
        s = 0
        d = nearest(SID_DECAY, min(d_ms + r_ms, 24000))
    r = nearest(SID_DECAY, r_ms) if r_ms < 1e8 else 15
    v.ad = a << 4 | d
    v.sr = min(s, 15) << 4 | r


def convert(imf):
    """IMF commands -> list of (register writes, delay in ticks) records."""
    regs = [0] * 256
    voices = [Voice() for _ in range(9)]
    shadow = {}
    records = []
    # Start: pulse width 50%, master volume on, filters off.
    init = []
    for sid in range(3):
        for vo in range(3):
            base = sid * 0x20 + vo * 7
            init += [(base + 2, 0x00), (base + 3, 0x08)]
        init += [(sid * 0x20 + 0x17, 0x00), (sid * 0x20 + 0x18, 0x0F)]
    pending = init
    touched = set()
    keyon_rise = set()

    for i, (reg, val, delay) in enumerate(imf):
        if 0xB0 <= reg <= 0xB8:
            c = reg - 0xB0
            if val & 0x20 and not (regs[reg] & 0x20):
                keyon_rise.add(c)
            touched.add(c)
        elif 0xA0 <= reg <= 0xA8:
            touched.add(reg - 0xA0)
        elif 0x40 <= reg <= 0x55 or 0x20 <= reg <= 0x35 or 0x60 <= reg <= 0x95 \
                or 0xE0 <= reg <= 0xF5 or 0xC0 <= reg <= 0xC8:
            touched.update(range(9))    # (cheap enough: recompute all)
        regs[reg] = val
        if delay == 0 and i + 1 < len(imf):
            continue                    # more writes at the same moment

        writes = pending
        pending = []
        for c in sorted(touched):
            v = voices[c]
            channel_voice(regs, c, v)
            v.gate = 1 if regs[0xB0 + c] & 0x20 else 0
            base = (c // 3) * 0x20 + (c % 3) * 7
            want = [(base + 0, v.freq & 0xFF), (base + 1, v.freq >> 8),
                    (base + 5, v.ad), (base + 6, v.sr)]
            for r_, x in want:
                if shadow.get(r_) != x:
                    writes.append((r_, x))
                    shadow[r_] = x
            ctrl = v.wave | v.gate
            if c in keyon_rise and shadow.get(base + 4, 0) & 1:
                writes.append((base + 4, v.wave))       # restart the envelope
                shadow[base + 4] = v.wave
            if shadow.get(base + 4) != ctrl:
                writes.append((base + 4, ctrl))
                shadow[base + 4] = ctrl
        touched.clear()
        keyon_rise.clear()
        # Records of at most 100 writes (the player indexes with Y).
        while len(writes) > 100:
            records.append((writes[:100], 0))
            writes = writes[100:]
        if records and not writes:
            w, d = records[-1]
            records[-1] = (w, d + delay)
        else:
            records.append((writes, delay))
    return records


def encode(records):
    out = bytearray()
    for writes, delay in records:
        if not writes:
            writes = [(0x18, 0x0F)]     # (a record needs at least one write)
        while delay > 0xFFFF:
            out += bytes([len(writes)]) + bytes(b for w in writes for b in w) + struct.pack("<H", 0xFFFF)
            delay -= 0xFFFF
            writes = [(0x18, 0x0F)]
        out += bytes([len(writes)]) + bytes(b for w in writes for b in w) + struct.pack("<H", delay)
    return bytes(out + b"\x00")         # 0: the end (start again)


PLAYER = r"""
; imf2sid player. init: silence the three SIDs, start the CIA timer at the
; IMF rate. play (every tick): when the wait is over, do the next record:
;   count, count * (register offset from $D400, value), delay (16 bits).
; A count of 0 starts the song again.
        .section .text,"ax",@progbits
        .globl init, play, data
ptr = $fb
init:
        ldx #$5f
        lda #0
1:      sta $d400,x
        dex
        bpl 1b
        lda #<(TIMER)
        sta $dc04
        lda #>(TIMER)
        sta $dc05
restart:
        lda #<data
        sta ptr
        lda #>data
        sta ptr+1
        lda #0
        sta wait
        sta wait+1
        rts
play:
        lda wait
        ora wait+1
        beq next
        lda wait
        bne 2f
        dec wait+1
2:      dec wait
        rts
next:
        ldy #0
        lda (ptr),y
        bne 3f
        jsr restart
        jmp next
3:      sta count
        iny
4:      lda (ptr),y
        tax
        iny
        lda (ptr),y
        iny
        sta $d400,x
        dec count
        bne 4b
        lda (ptr),y
        sta wait
        iny
        lda (ptr),y
        sta wait+1
        iny
        tya
        clc
        adc ptr
        sta ptr
        bcc 5f
        inc ptr+1
5:      lda wait
        ora wait+1
        beq next            ; no delay: the next record now
        lda wait            ; delay d: the next record d ticks from now
        bne 6f
        dec wait+1
6:      dec wait
        rts
count:  .byte 0
wait:   .short 0
data:
"""


def build_player(tmp):
    timer = round(PAL_CLOCK / IMF_RATE) - 1
    src = os.path.join(tmp, "player.s")
    open(src, "w").write(f"TIMER = {timer}\n" + PLAYER)
    open(os.path.join(tmp, "player.ld"), "w").write(
        "SECTIONS { . = 0x1000; .text : { *(.text) } }\n")
    obj, elf, bin_ = (os.path.join(tmp, n) for n in ("player.o", "player.elf", "player.bin"))
    subprocess.check_call([f"{BIN}/mos-common-clang", "-c", "-o", obj, src])
    subprocess.check_call([f"{BIN}/ld.lld", "-e", "init", "-T", os.path.join(tmp, "player.ld"), "-o", elf, obj])
    subprocess.check_call([f"{BIN}/llvm-objcopy", "-O", "binary", elf, bin_])
    syms = {}
    for line in subprocess.check_output([f"{BIN}/llvm-nm", elf], text=True).splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    code = open(bin_, "rb").read()
    assert syms["data"] == 0x1000 + len(code), "data must follow the player"
    return code, syms["init"], syms["play"]


def psid(name, code, init, play, data, model):
    hdr = bytearray(0x7C)
    hdr[0:4] = b"PSID"
    struct.pack_into(">HHHHHHHI", hdr, 4, 4, 0x7C, 0, init, play, 1, 1, 1)  # speed bit 0: CIA
    hdr[0x16:0x16 + 32] = name.encode()[:32].ljust(32, b"\0")
    hdr[0x36:0x36 + 32] = b"Bobby Prince (AdLib->SID preview)"[:32]
    hdr[0x56:0x56 + 32] = b"1992 id Software".ljust(32, b"\0")
    m = 1 if model == "6581" else 2
    flags = (1 << 2) | (m << 4) | (m << 6) | (m << 8)     # PAL; SID models 1-3
    struct.pack_into(">H", hdr, 0x76, flags)
    hdr[0x7A] = 0x42                    # second SID at $D420
    hdr[0x7B] = 0x44                    # third SID at $D440
    body = struct.pack("<H", 0x1000) + code + data
    assert 0x1000 + len(code) + len(data) < 0xD000, f"{name}: too big"
    return bytes(hdr) + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audiohed")
    ap.add_argument("audiot")
    ap.add_argument("outdir")
    ap.add_argument("--song")
    ap.add_argument("--model", default="8580", choices=("6581", "8580"))
    args = ap.parse_args()
    hed = open(args.audiohed, "rb").read()
    au = open(args.audiot, "rb").read()
    off = struct.unpack(f"<{len(hed) // 4}I", hed)
    os.makedirs(args.outdir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        code, init, play = build_player(tmp)
        for k, name in enumerate(SONGS):
            if args.song and name != args.song.upper():
                continue
            a, b = off[STARTMUSIC + k], off[STARTMUSIC + k + 1]
            length = struct.unpack_from("<H", au, a)[0] if b > a else 0
            if not length:
                continue                # (not in this version of the game)
            imf = [struct.unpack_from("<BBH", au, a + 2 + i) for i in range(0, length - 3, 4)]
            data = encode(convert(imf))
            path = os.path.join(args.outdir, name + ".sid")
            open(path, "wb").write(psid(name, code, init, play, data, args.model))
            secs = sum(d for _, _, d in imf) / IMF_RATE
            print(f"{path}: {secs:5.1f}s, {len(data)} bytes of SID data")


if __name__ == "__main__":
    main()
