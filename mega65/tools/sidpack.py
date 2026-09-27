"""sidpack: what the music converters (imf2sid.py, midi2sid.py) share: the
6502 player, the encoding of its data and the PSID v4 file (3 SIDs).

The player plays records of SID register writes, each followed by a delay in
ticks, from a CIA timer interrupt at a fixed tick rate; at the end it starts
again. It is assembled with llvm-mos.
"""
import os
import struct
import subprocess

BIN = os.path.expanduser("~/opt/llvm-mos/bin")
PAL_CLOCK = 985248


def encode(records, volume=0x0F):
    """(writes, delay) records -> the player's data: count, count * (register
    offset from $D400, value), delay in ticks (16 bits); a count of 0 ends."""
    out = bytearray()
    for writes, delay in records:
        if not writes:
            writes = [(0x18, volume)]   # (a record needs at least one write)
        while delay > 0xFFFF:
            out += bytes([len(writes)]) + bytes(b for w in writes for b in w) + struct.pack("<H", 0xFFFF)
            delay -= 0xFFFF
            writes = [(0x18, volume)]
        out += bytes([len(writes)]) + bytes(b for w in writes for b in w) + struct.pack("<H", delay)
    return bytes(out + b"\x00")         # 0: the end (start again)


PLAYER = r"""
; Player. init: silence the three SIDs, start the CIA timer at the tick
; rate. play (every tick): when the wait is over, do the next record:
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


def build_player(tmp, rate):
    """Assemble the player (at $1000, the data right after it) for `rate` ticks/s."""
    timer = round(PAL_CLOCK / rate) - 1
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


def psid(name, author, code, init, play, data, model):
    """A PSID v4 file: PAL, CIA-timed, SIDs at $D400, $D420 and $D440."""
    hdr = bytearray(0x7C)
    hdr[0:4] = b"PSID"
    struct.pack_into(">HHHHHHHI", hdr, 4, 4, 0x7C, 0, init, play, 1, 1, 1)  # speed bit 0: CIA
    hdr[0x16:0x16 + 32] = name.encode()[:32].ljust(32, b"\0")
    hdr[0x36:0x36 + 32] = author.encode()[:32].ljust(32, b"\0")
    hdr[0x56:0x56 + 32] = b"1992 id Software".ljust(32, b"\0")
    m = 1 if model == "6581" else 2
    flags = (1 << 2) | (m << 4) | (m << 6) | (m << 8)     # PAL; SID models 1-3
    struct.pack_into(">H", hdr, 0x76, flags)
    hdr[0x7A] = 0x42                    # second SID at $D420
    hdr[0x7B] = 0x44                    # third SID at $D440
    body = struct.pack("<H", 0x1000) + code + data
    assert 0x1000 + len(code) + len(data) < 0xD000, f"{name}: too big"
    return bytes(hdr) + body
