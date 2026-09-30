#!/usr/bin/env python3
"""Check a make test-save run: the serial log (wolf.ser) and SAVES.DAT."""
import struct
import sys

from m65common import read, report

log = [l.strip() for l in read(sys.argv[1]).decode("latin-1").splitlines()]
events = [l for l in log if l == "SAVED" or l.startswith("SAVE FAILED") or l.startswith("SAVEGAM")]
created = "CREATED" in log
SLOT = 32768
saves = read(sys.argv[2])
config, save = saves[:SLOT], saves[SLOT:2 * SLOT]        # slot 0, slot 1 (SAVEGAM0)
magic, length = save[:4], struct.unpack("<I", save[4:8])[0]

print("serial log:", ", ".join(events))
print(f"save game 0: {length} bytes")
report([
    ("SAVES.DAT made by the game", created),
    ("config written on leaving the menu for the new game", events[:1] == ["SAVED"]),
    ("the save game written", events[1:2] == ["SAVED"]),
    ("the save game read back", "SAVEGAM0.WL1" in events[2:]),
    ("loaded without complaint (the menu closed: config written)",
     events[-1:] == ["SAVED"] and len(events) >= 4),
    ("no write failed", not any(e.startswith("SAVE FAILED") for e in events)),
    ("SAVES.DAT: the whole file", len(saves) == 11 * SLOT),
    ("SAVES.DAT: the config (slot 0)", config[:4] == b"WM65" and config[16:18] == b"\xfa\xfe"),
    ("SAVES.DAT: save game 0 (slot 1): header", magic == b"WM65" and 0 < length <= SLOT - 16),
    ("SAVES.DAT: save game 0: the name", save[16:18] == b"a\0"),
    ("SAVES.DAT: the other slots empty", not any(saves[2 * SLOT:])),
])
