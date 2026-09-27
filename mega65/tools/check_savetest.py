#!/usr/bin/env python3
"""Check a make test-save run: the serial log (wolf.ser) and the save file."""
import struct
import sys

log = [l.strip() for l in open(sys.argv[1], "rb").read().decode("latin-1").splitlines()]
events = [l for l in log if l == "SAVED" or l.startswith("SAVE FAILED") or l.startswith("SAVEGAM")]
save = open(sys.argv[2], "rb").read()
magic, length = save[:4], struct.unpack("<I", save[4:8])[0]

checks = [
    ("config written on leaving the menu for the new game", events[:1] == ["SAVED"]),
    ("the save game written", events[1:2] == ["SAVED"]),
    ("the save game read back", "SAVEGAM0.WL1" in events[2:]),
    ("loaded without complaint (the menu closed: config written)",
     events[-1:] == ["SAVED"] and len(events) >= 4),
    ("no write failed", not any(e.startswith("SAVE FAILED") for e in events)),
    ("SAVEGAM0.WL1: header", magic == b"WM65" and 0 < length <= 32768 - 16),
    ("SAVEGAM0.WL1: the name", save[16:18] == b"a\0"),
]
print("serial log:", ", ".join(events))
print(f"SAVEGAM0.WL1: {length} bytes of save")
bad = 0
for name, ok in checks:
    print(("PASS  " if ok else "FAIL  ") + name)
    bad += not ok
sys.exit(1 if bad else 0)
