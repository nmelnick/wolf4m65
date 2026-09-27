#!/usr/bin/env python3
"""midi2sid: Bobby Prince's MIDI originals of the Wolfenstein 3-D music as
3-SID PSID files (play them with VICE's vsid).

usage: midi2sid.py OUTDIR FILE.mid... [--model 6581|8580]

The MIDI files name each part's instrument (General MIDI programs), carry
velocities, channel volumes and pitch bends, and have a real drum channel,
all of which the game's AdLib data has lost. Here:
  - every note gets one of the 9 SID voices (3 SIDs at $D400, $D420, $D440):
    a free one, preferably the one its channel used last; when all are busy
    (rare: most songs peak at 9 notes or fewer) the oldest note gives way;
  - each General MIDI instrument has a SID patch (PATCHES): waveform, pulse
    width and envelope chosen for its character (a clarinet is a square
    wave, an oboe a narrow pulse, brass and strings sawtooth, flutes
    triangle; plucked and struck sounds decay, blown and bowed ones hold);
  - drums (channel 10) are SID drum sounds (drum_patch): kick and toms are
    triangle waves falling in pitch, snares, hats and cymbals noise;
  - loudness: the SID has no per-voice volume, only the envelope's sustain
    level, so velocity x channel volume x expression sets that. Attacks
    always peak at full level; patches decay from it quickly;
  - pitch bends (with the bend range from RPN 0) move the voices' pitch;
  - every note starts with a "hard restart": 10ms before it, the voice's
    envelope is forced down, so the SID reliably starts the new attack
    (known in advance, since this is converted offline).
The SID register writes are then played by the player in sidpack.py at 200
ticks per second (5ms timing).
"""
import argparse
import os
import re
import struct
import tempfile

from sidpack import build_player, encode, psid

RATE = 200                      # player ticks per second
PAL_CLOCK = 985248
TRI, SAW, PUL, NOI = 0x10, 0x20, 0x40, 0x80


# ---- reading MIDI -------------------------------------------------------------

def varlen(d, p):
    v = 0
    while True:
        b = d[p]
        p += 1
        v = v << 7 | (b & 0x7F)
        if not b & 0x80:
            return v, p


def read_midi(path):
    """-> (events sorted by time: (seconds, status, data bytes)), end in seconds."""
    d = open(path, "rb").read()
    assert d[:4] == b"MThd", path
    hlen, _, ntrk, div = struct.unpack(">IHHH", d[4:14])
    assert not div & 0x8000, "SMPTE time is not supported"
    pos = 8 + hlen
    raw, tempos, end = [], [], 0
    for trk in range(ntrk):
        assert d[pos:pos + 4] == b"MTrk", path
        length = struct.unpack(">I", d[pos + 4:pos + 8])[0]
        p, stop = pos + 8, pos + 8 + length
        pos = stop
        t = status = seq = 0
        while p < stop:
            delta, p = varlen(d, p)
            t += delta
            b = d[p]
            if b == 0xFF:
                typ = d[p + 1]
                n, p = varlen(d, p + 2)
                if typ == 0x51:
                    tempos.append((t, int.from_bytes(d[p:p + 3], "big")))
                elif typ == 0x2F:
                    end = max(end, t)
                p += n
            elif b in (0xF0, 0xF7):
                n, p = varlen(d, p + 1)
                p += n
            else:
                if b & 0x80:
                    status = b
                    p += 1
                n = 1 if status & 0xF0 in (0xC0, 0xD0) else 2
                data = d[p:p + n]
                p += n
                # note-offs first at the same moment (they free voices)
                on = status & 0xF0 == 0x90 and data[1] > 0
                raw.append((t, on, trk, seq, status, data))
                seq += 1
    raw.sort()
    tempos.sort()

    def seconds(tick):
        s, last, tempo = 0.0, 0, 500000
        for tt, tp in tempos:
            if tt >= tick:
                break
            s += (tt - last) * tempo / div / 1e6
            last, tempo = tt, tp
        return s + (tick - last) * tempo / div / 1e6

    return [(seconds(t), st, data) for t, _, _, _, st, data in raw], seconds(end)


# ---- SID patches ------------------------------------------------------------------

class Patch:
    """A SID sound: waveform, pulse width (12 bits), envelope nibbles (s is the
    sustain level at full volume), and for drums a fixed pitch (Hz, or a raw
    frequency register value for noise) and a pitch sweep: [(ticks, factor)]."""
    def __init__(self, wave, a, d, s, r, pw=0x800, hz=None, raw=None, sweep=()):
        self.wave, self.a, self.d, self.s, self.r = wave, a, d, s, r
        self.pw, self.hz, self.raw, self.sweep = pw, hz, raw, sweep


def P(wave, a, d, s, r, pw=0x800, **kw):
    return Patch(wave, a, d, s, r, pw, **kw)


DRUMSWEEP = [(2, 0.85), (4, 0.72), (7, 0.62), (11, 0.55)]

# General MIDI program -> patch. (Program numbers from 0.)
PATCHES = {}
for prog, patch in [
    # pianos
    ((0, 1, 2, 3), P(PUL, 0, 10, 0, 8, 0x500)),
    ((4, 5), P(TRI, 0, 10, 2, 8)),
    ((6,), P(PUL, 0, 8, 0, 6, 0x200)),
    ((7,), P(PUL, 0, 7, 3, 4, 0x300)),
    # chromatic percussion
    ((8, 9, 10), P(TRI, 0, 9, 0, 9)),
    ((11,), P(TRI, 0, 10, 0, 10)),
    ((12,), P(TRI, 0, 7, 0, 6)),
    ((13,), P(TRI, 0, 6, 0, 5)),
    ((14,), P(TRI, 0, 11, 0, 11)),
    ((15,), P(PUL, 0, 9, 0, 8, 0x300)),
    # organs, accordion, harmonica
    ((16, 17, 18, 19, 20), P(PUL, 0, 0, 15, 3, 0x800)),
    ((21, 23), P(PUL, 2, 0, 15, 4, 0x400)),
    ((22,), P(PUL, 2, 0, 15, 4, 0x300)),
    # guitars
    ((24,), P(PUL, 0, 9, 0, 7, 0x400)),
    ((25,), P(PUL, 0, 9, 0, 8, 0x300)),
    ((26,), P(PUL, 0, 8, 2, 6, 0x600)),
    ((27,), P(PUL, 0, 9, 2, 7, 0x500)),
    ((28,), P(PUL, 0, 5, 0, 3, 0x400)),
    ((29, 30), P(SAW, 0, 7, 10, 6)),
    ((31,), P(TRI, 0, 9, 0, 8)),
    # basses
    ((32,), P(TRI, 0, 8, 4, 4)),
    ((33, 34), P(PUL, 0, 8, 6, 4, 0x600)),
    ((35,), P(TRI, 1, 6, 10, 5)),
    ((36, 37), P(PUL, 0, 7, 4, 4, 0x400)),
    ((38, 39), P(SAW, 0, 7, 8, 4)),
    # strings
    ((40, 41, 42), P(SAW, 3, 5, 13, 6)),
    ((43,), P(SAW, 2, 5, 13, 5)),
    ((44,), P(SAW, 2, 5, 13, 6)),
    ((45,), P(PUL, 0, 6, 0, 5, 0x600)),
    ((46,), P(TRI, 0, 9, 0, 9)),
    ((47,), P(TRI, 0, 9, 0, 9)),
    # ensembles, voices
    ((48, 49), P(SAW, 5, 6, 12, 8)),
    ((50, 51), P(PUL, 4, 6, 12, 8, 0x600)),
    ((52, 53, 54), P(TRI, 5, 4, 13, 7)),
    ((55,), P(SAW, 0, 7, 0, 6)),
    # brass
    ((56,), P(SAW, 1, 4, 12, 4)),
    ((57,), P(SAW, 2, 5, 12, 5)),
    ((58,), P(PUL, 1, 5, 12, 4, 0x700)),
    ((59,), P(PUL, 1, 4, 12, 4, 0x200)),
    ((60,), P(PUL, 3, 5, 12, 6, 0x700)),
    ((61,), P(SAW, 2, 4, 12, 5)),
    ((62, 63), P(SAW, 1, 5, 11, 5)),
    # reeds
    ((64, 65, 66, 67), P(PUL, 1, 4, 12, 4, 0x500)),
    ((68, 69), P(PUL, 1, 3, 12, 4, 0x280)),
    ((70,), P(PUL, 1, 4, 12, 4, 0x400)),
    ((71,), P(PUL, 1, 3, 12, 4, 0x800)),
    # pipes
    (tuple(range(72, 80)), P(TRI, 2, 3, 13, 4)),
    # synth leads
    ((80,), P(PUL, 0, 4, 12, 4, 0x800)),
    ((81,), P(SAW, 0, 4, 12, 4)),
    ((82,), P(TRI, 1, 3, 13, 4)),
    ((83, 84, 85, 86, 87), P(SAW, 0, 4, 12, 4)),
    # pads
    ((88, 89, 90, 91, 93, 94, 95), P(PUL, 7, 6, 12, 9, 0x600)),
    ((92,), P(SAW, 8, 6, 12, 9)),
    # effects
    (tuple(range(96, 104)), P(TRI, 2, 6, 10, 8)),
    # ethnic
    ((104, 105, 106, 107), P(PUL, 0, 8, 0, 7, 0x300)),
    ((108,), P(TRI, 0, 7, 0, 6)),
    ((109,), P(SAW, 1, 0, 13, 3)),
    ((110,), P(SAW, 2, 5, 13, 5)),
    ((111,), P(PUL, 1, 0, 12, 4, 0x300)),
    # percussive
    ((112,), P(TRI, 0, 8, 0, 8)),
    ((113,), P(TRI, 0, 6, 0, 5)),
    ((114,), P(TRI, 0, 8, 0, 7)),
    ((115,), P(TRI, 0, 3, 0, 2)),
    ((116, 117, 118), P(TRI, 0, 7, 0, 5, sweep=DRUMSWEEP)),
    ((119,), P(NOI, 11, 0, 15, 1, raw=0xC000)),          # reverse cymbal: swells
    # sound effects
    (tuple(range(120, 128)), P(NOI, 0, 8, 0, 8, raw=0x8000)),
]:
    for p in prog:
        PATCHES[p] = patch
assert len(PATCHES) == 128

TOMS = {41: 80, 43: 95, 45: 110, 47: 130, 48: 150, 50: 180}


def drum_patch(note):
    """General MIDI drum note -> patch."""
    if note in (35, 36):                                    # kick
        return P(TRI, 0, 6, 0, 0, hz=160, sweep=[(2, 0.7), (4, 0.5), (6, 0.38), (9, 0.31)])
    if note in TOMS:
        return P(TRI, 0, 7, 0, 5, hz=TOMS[note], sweep=[(3, 0.85), (6, 0.75), (10, 0.7)])
    if note in (38, 40):                                    # snares
        return P(NOI, 0, 5, 0, 5, raw=0x1C00)
    if note == 37:                                          # side stick
        return P(NOI, 0, 2, 0, 2, raw=0x5000)
    if note == 39:                                          # clap
        return P(NOI, 0, 4, 0, 4, raw=0x3000)
    if note in (42, 44):                                    # closed / pedal hi-hat
        return P(NOI, 0, 2, 0, 2, raw=0xE000)
    if note == 46:                                          # open hi-hat
        return P(NOI, 0, 6, 0, 6, raw=0xE000)
    if note in (49, 57, 52, 55):                            # crashes, china, splash
        return P(NOI, 0, 10, 0, 10, raw=0xC000)
    if note in (51, 59):                                    # rides
        return P(NOI, 0, 8, 0, 7, raw=0xF000)
    if note == 56:                                          # cowbell
        return P(PUL, 0, 5, 0, 4, hz=560)
    if note == 54:                                          # tambourine
        return P(NOI, 0, 4, 0, 4, raw=0xD000)
    return P(NOI, 0, 4, 0, 4, raw=0x8000)


def freq_reg(hz):
    f = round(hz * 16777216 / PAL_CLOCK)
    while f > 0xFFFF:
        f >>= 1
    return max(f, 1)


def note_hz(note, bend=0.0):
    return 440.0 * 2 ** ((note - 69 + bend) / 12)


# ---- conversion -------------------------------------------------------------------

class Voice:
    def __init__(self, k):
        self.base = (k // 3) * 0x20 + (k % 3) * 7
        self.note = None                # (channel, note) while playing
        self.start = -1                 # tick of the current/last note-on
        self.released = -1              # tick of the last note-off
        self.channel = None
        self.patch = None
        self.hz = 0
        self.level = 1.0


def convert(events, end, resumable=False, sidvolume=0x0F):
    """resumable: every note writes all of its voice's setup, so that playing
    can start at any record (for the game, which resumes songs)."""
    out = []                            # (tick, order, register, value)
    voices = [Voice(k) for k in range(9)]
    program = [0] * 16
    volume = [100] * 16
    expression = [127] * 16
    bend = [0.0] * 16
    bendrange = [2.0] * 16
    rpn = [(127, 127)] * 16
    last_tick = 0

    def w(t, order, v, reg, val, force=False):
        out.append((t, order, v.base + reg, val & 0xFF, force and resumable))

    def level(ch, vel):
        return ((vel / 127) * volume[ch] / 127 * expression[ch] / 127) ** 0.5

    def sustain(v):
        return min(15, round(v.patch.s * v.level))

    def voice_pitch(v, ch):
        p = v.patch
        return p.hz if p.hz else note_hz(v.note[1], bend[ch])

    def note_off(t, v):
        if v.note is None:
            return
        t = max(t, v.start + 1)
        w(t, 0, v, 4, v.patch.wave)
        v.note = None
        v.released = t

    def pick(ch, patch):
        free = [v for v in voices if v.note is None]
        if free:
            same = [v for v in free if v.channel == ch and v.patch is patch]
            return same[0] if same else min(free, key=lambda v: v.released)
        return min(voices, key=lambda v: v.start)           # the oldest note gives way

    for sec, status, data in events:
        t = round(sec * RATE) + 3       # (+3: room for the first hard restart)
        last_tick = max(last_tick, t)
        kind, ch = status & 0xF0, status & 15
        if kind == 0x90 and data[1] > 0:
            note, vel = data
            for v in voices:
                if v.note == (ch, note):
                    note_off(t, v)
            patch = drum_patch(note) if ch == 9 else PATCHES[program[ch]]
            v = pick(ch, patch)
            if v.note is not None:
                note_off(t - 2, v)
            # hard restart: envelope down fast, gate off, 2 ticks before
            hr = max(t - 2, v.start + 1, 0)
            w(hr, 0, v, 4, (v.patch.wave if v.patch else TRI))
            w(hr, 1, v, 5, 0x00)
            w(hr, 1, v, 6, 0x00)
            v.note, v.channel, v.patch, v.start = (ch, note), ch, patch, t
            v.level = level(ch, vel)
            hz = voice_pitch(v, ch)
            f = patch.raw if patch.raw else freq_reg(hz)
            # (resumable: all of the voice's setup, even what it has already)
            w(t, 1, v, 0, f, True)
            w(t, 1, v, 1, f >> 8, True)
            w(t, 1, v, 2, patch.pw, True)
            w(t, 1, v, 3, patch.pw >> 8, True)
            w(t, 1, v, 5, patch.a << 4 | patch.d, True)
            w(t, 1, v, 6, sustain(v) << 4 | patch.r, True)
            w(t, 2, v, 4, patch.wave | 1, True)
            for dt, factor in patch.sweep:
                f = freq_reg(hz * factor)
                w(t + dt, 1, v, 0, f)
                w(t + dt, 1, v, 1, f >> 8)
        elif kind == 0x80 or kind == 0x90:
            for v in voices:
                if v.note == (ch, data[0]):
                    note_off(t, v)
        elif kind == 0xC0:
            program[ch] = data[0]
        elif kind == 0xB0:
            cc, val = data
            if cc in (7, 11):
                (volume if cc == 7 else expression)[ch] = val
                for v in voices:
                    if v.note and v.note[0] == ch:
                        w(t, 1, v, 6, sustain(v) << 4 | v.patch.r)
            elif cc == 101:
                rpn[ch] = (val, rpn[ch][1])
            elif cc == 100:
                rpn[ch] = (rpn[ch][0], val)
            elif cc == 6 and rpn[ch] == (0, 0):
                bendrange[ch] = val
        elif kind == 0xE0:
            bend[ch] = ((data[1] << 7 | data[0]) - 8192) / 8192 * bendrange[ch]
            for v in voices:
                if v.note and v.note[0] == ch and not v.patch.hz and not v.patch.raw:
                    f = freq_reg(note_hz(v.note[1], bend[ch]))
                    w(t, 1, v, 0, f)
                    w(t, 1, v, 1, f >> 8)

    # Records: the writes of each tick (the last write to a register wins,
    # except the control registers, whose every change counts), without the
    # ones that change nothing (unless forced: a note's setup).
    init = []
    for sid in range(3):
        init += [(sid * 0x20 + 0x17, 0x00), (sid * 0x20 + 0x18, sidvolume)]
    out.sort(key=lambda e: (e[0], e[1]))
    ticks = {}
    for t, order, reg, val, force in out:
        ticks.setdefault(t, []).append((reg, val, force))
    shadow = {}
    records = [(init, 0)]
    prev = 0
    for t in sorted(ticks):
        writes, seen = [], {}
        for reg, val, force in ticks[t]:
            if reg % 7 == 4 and reg % 0x20 < 0x15:
                writes.append((reg, val, force))
            else:
                seen[reg] = (val, force or seen.get(reg, (0, False))[1])
        writes = [(r, v, f) for r, (v, f) in seen.items()] + writes    # settings before gates
        kept = []
        for reg, val, force in writes:
            if force or shadow.get(reg) != val:
                kept.append((reg, val))
                shadow[reg] = val
        if not kept:
            continue
        w_, d_ = records[-1]
        records[-1] = (w_, d_ + t - prev)
        while len(kept) > 100:
            records.append((kept[:100], 0))
            kept = kept[100:]
        records.append((kept, 0))
        prev = t
    loop = max(round(end * RATE) + 3, last_tick + RATE // 2)
    w_, d_ = records[-1]
    records[-1] = (w_, d_ + loop - prev)
    return records, loop / RATE


# The game's songs (musicnames in audiowl6.h, in order) and the numbers of
# their MIDI files ("NN - title.mid"). Checked against the shareware AdLib
# data by notes and timing; the rest by title and the game's level table.
GAME_SONGS = [
    ("CORNER", 8), ("DUNGEON", 13), ("WARMARCH", 7), ("GETTHEM", 3),
    ("HEADACHE", 12), ("HITLWLTZ", 24), ("INTROCW3", 14), ("NAZI_NOR", 1),
    ("NAZI_OMI", 9), ("POW", 5), ("SALUTE", 25), ("SEARCHN", 4),
    ("SUSPENSE", 6), ("VICTORS", 26), ("WONDERIN", 2), ("FUNKYOU", 20),
    ("ENDLEVEL", 21), ("GOINGAFT", 11), ("PREGNANT", 10), ("ULTIMATE", 18),
    ("NAZI_RAP", 15), ("ZEROHOUR", 17), ("TWELFTH", 16), ("ROSTER", 23),
    ("URAHERO", 22), ("VICMARCH", 27), ("PACMAN", 19),
]


# The music's SID volume in the game (of 15): a little under full, so the
# sound effects (audio DMA and the fourth SID) are not drowned out.
GAME_VOLUME = 12


def write_game(path, mididir):
    """MUSIC.DAT for the game (mega65/m65_sd.cpp, m65_music.s):
         "WMUS", version 1, song count, ticks per second (16 bits),
         a 32-bit file offset per song (0: no such song), the songs' data
         (the player's records, as in the .sid files).
    Songs whose MIDI file is missing are left out."""
    streams, found = [], 0
    for name, num in GAME_SONGS:
        files = [f for f in os.listdir(mididir)
                 if re.match(rf"0*{num}\s*-.*\.mid$", f, re.I)]
        if not files:
            streams.append(b"")
            continue
        events, end = read_midi(os.path.join(mididir, files[0]))
        records, _ = convert(events, end, resumable=True, sidvolume=GAME_VOLUME)
        streams.append(encode(records, GAME_VOLUME))
        found += 1
    head = b"WMUS" + struct.pack("<BBH", 1, len(streams), RATE)
    pos = len(head) + 4 * len(streams)
    offsets, body = [], b""
    for data in streams:
        assert len(data) < 0x10000, "a song must be under 64KB (the game resumes by offset)"
        offsets.append(pos + len(body) if data else 0)
        body += data
    open(path, "wb").write(head + struct.pack(f"<{len(offsets)}I", *offsets) + body)
    print(f"{path}: {found} of {len(GAME_SONGS)} songs, {len(head) + 4 * len(streams) + len(body)} bytes")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir", help="where the .sid files go (with --game: MUSIC.DAT's path)")
    ap.add_argument("midi", nargs="*", help="the .mid files (with --game: their directory)")
    ap.add_argument("--model", default="8580", choices=("6581", "8580"))
    ap.add_argument("--game", action="store_true", help="write MUSIC.DAT for the game")
    args = ap.parse_args()
    if args.game:
        write_game(args.outdir, args.midi[0] if args.midi else ".")
        return
    os.makedirs(args.outdir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        code, init, play = build_player(tmp, RATE)
        for path in args.midi:
            title = os.path.splitext(os.path.basename(path))[0]
            name = re.sub(r"^\d+\s*-\s*", "", title)
            events, end = read_midi(path)
            records, secs = convert(events, end)
            data = encode(records)
            out = os.path.join(args.outdir, re.sub(r"\W+", "_", title).strip("_") + ".sid")
            open(out, "wb").write(psid(name, "Bobby Prince (MIDI->SID)", code, init, play,
                                       data, args.model))
            print(f"{out}: {secs:5.1f}s, {len(data)} bytes of SID data")


if __name__ == "__main__":
    main()
