# Wolfenstein 3-D for the MEGA65

A port of Wolf4SDL to the MEGA65, built with llvm-mos. Work in progress:
the shareware episode loads, the sign-on screen, title and demo run, and the
3D view matches the original code pixel for pixel. There is no sound yet,
and the game is slow: about 3 frames per second in Xemu's timing (`make
fps`; real hardware may differ, DMA especially).

## Running it on a MEGA65

Copy these files to the **root directory** of the MEGA65's SD card (`make
sdcard` gathers them in `build/sdcard/`):

| File | What it is |
| --- | --- |
| `WOLF3D.D81` | disk image with the program, `WOLF` |
| `WOLF.OVL` | the program's code overlays |
| `WOLF.DAT` | the program's data |
| `SIGNON.BIN`, `TABLES.BIN` | the sign-on screen and precomputed tables |
| `AUDIOHED.WL1`, `AUDIOT.WL1`, `GAMEMAPS.WL1`, `MAPHEAD.WL1`, `VGADICT.WL1`, `VGAGRAPH.WL1`, `VGAHEAD.WL1`, `VSWAP.WL1` | the shareware game data (v1.4), not included: bring your own |

Then, on the MEGA65:

1. Mount `WOLF3D.D81` as drive 8: type `MOUNT "WOLF3D.D81"`, or hold
   RESTORE for the Freezer, press `0` to choose drive 0's disk image, pick
   `WOLF3D.D81`, and press `F3` to resume.
2. Type `RUN "WOLF"`.

The game reads its files from the SD card's root directory, whichever disk
image is mounted. If a file is missing, it says which one on the screen and
stops with a red border.

Keys: cursor keys to move, CTRL to fire, ALT to strafe, SHIFT to run,
SPACE to open doors, ESC for the menu, RETURN to choose. The number in the
top left corner of the 3D view is the frame rate (a development aid).

## Building

You need:

- [llvm-mos](https://github.com/llvm-mos/llvm-mos-sdk) in `~/opt/llvm-mos`
- the shareware data (`*.WL1`) in the repository root (git ignores them)
- Python 3; mtools (`mcopy`) and Xemu (`xemu-xmega65`) for the tests; VICE's
  `c1541` for the disk image; `g++` for the host reference; Pillow (PIL) for
  some checks
- for the tests: an Xemu SD card image in `~/.local/share/xemu-lgb/mega65/`
  (Xemu makes one on its first run). The tests use a copy, `build/sd.img`,
  with the game's files added; your own image is left alone.

## Make targets

Run them in `mega65/`.

| Command | What it does |
| --- | --- |
| `make wolf.prg` | build the game: `wolf.prg`, `build/game/WOLF.OVL`, `build/game/WOLF.DAT` |
| `make sdcard` | everything for the SD card in `build/sdcard/`, with `WOLF3D.D81` |
| `make dist` | `build/wolf3d-mega65.zip`: the same without the game data, plus this README |
| `make run-xemu` | play it in Xemu (a window), on `build/sd.img` |
| `make run-wolf` | run it headless for `RUNSECS` seconds (60); keeps `wolf.png` (screenshot) and `wolf.ser` (debug output) |
| `make fps` | run the first demo and print the frame times |
| `make profile` | sample where the time goes during the demo (`FRAME=N`: to frame N) |
| `make FRAME=N check-frame` | compare demo frame N with the original code's, pixel for pixel (writes `frame_N.png`) |
| `make test-host test-proxy test-ovl test-dos test-load test-huff test-ca test-title` | the other tests |
| `make sid` | the game's AdLib music as 3-SID files (`build/sid/`) |
| `make sid-midi` | the MIDI originals (put the `.mid` files in `mega65/`) as 3-SID files (`build/sid-midi/`) |
| `make clean` | remove what was built |

Options:

- `KEYS='{ms,scancode},...'` presses keys at those times (in ms) through the
  keyboard's virtual-key register, for headless runs. Scan codes are
  column × 8 + row of the C65 keyboard matrix (SPACE is `0x3C`); `0x7F`
  releases.
- `OVLTRACE=1` logs every overlay call on the debug serial port;
  `tools/ovltrace.py wolf.prg.elf wolf.ser` names them.
- `SIDMODEL=6581` for the older SID in the music files (default 8580).

## How it fits

The MEGA65 has 384KB of chip RAM and 8MB of attic RAM, but a 6502-style CPU
sees 64KB at a time. The game code (about 350KB) runs from 8KB overlays at
`$4000`, paged in from attic RAM; the game's files are loaded whole into
attic RAM; large arrays (maps, graphics) live in far memory and are reached
through C++ proxy types. `tools/ovlgen.py` describes the memory layout.
