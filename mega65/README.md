# Wolfenstein 3-D for the MEGA65

A port of Wolf4SDL to the MEGA65, built with llvm-mos. Work in progress:
the shareware episode and the registered six play (with saving and loading), the sign-on screen, title and demo run, and the
3D view is drawn with the MEGA65's DMA scaling (close to the original,
not pixel-exact). The music plays on three SIDs (from the MIDI originals,
see below); the digitized sound effects (gunfire, enemy calls, doors) play
on the audio DMA channels, and the AdLib-only effects (item pickups and the
like) on the fourth SID. It runs at about 8-10 frames per second (`make
fps` measures the demo in Xemu, which is now close to the real machine).

## Running it on a MEGA65

There are two builds, for the two sets of game data (bring your own: they
are not included):

| Build | Game data | Disk image | Port's files |
| --- | --- | --- | --- |
| shareware (`make`) | `*.WL1`, the first episode, v1.4 | `WOLF3D.D81` | `WOLF1.OVL`, `WOLF1.DAT` |
| registered (`make VERSION=wl6`) | `*.WL6`, all six episodes, v1.4 GT/id/Activision (the one sold today, e.g. on Steam or GOG) | `WOLF3D6.D81` | `WOLF6.OVL`, `WOLF6.DAT` |

(`make VERSION=wl6apo` is for the registered v1.4 as Apogee sold it; its
graphics are numbered differently.) Both can share one SD card.

Copy the disk image and the folder `WOLF4M65` to the **root directory** of
the MEGA65's SD card (`make sdcard` gathers them in `build/sdcard/`, or
`build/sdcard-wl6/`). In `WOLF4M65`:

| File | What it is |
| --- | --- |
| `WOLF1.OVL` / `WOLF6.OVL` | the program's code overlays |
| `WOLF1.DAT` / `WOLF6.DAT` | the program's data |
| `SIGNON.BIN`, `TABLES.BIN` | the sign-on screen and precomputed tables (both builds) |
| `AUDIOHED`, `AUDIOT`, `GAMEMAPS`, `MAPHEAD`, `VGADICT`, `VGAGRAPH`, `VGAHEAD`, `VSWAP` (`.WL1` / `.WL6`) | your game data |
| `MUSIC.DAT` | the music (optional: without it there is no music), built from your own copy of the MIDI originals (both builds) |
| `SFX1.DAT` / `SFX6.DAT` | the AdLib sound effects converted for a SID (optional), built from your game data (`AUDIOT`) |

The game keeps the save games, settings, key setup and high scores in
`SAVES1.DAT` / `SAVES6.DAT` in the same folder, which it makes (empty,
352KB) the first time it runs, so copying a newer build over the folder
leaves them alone. The settings are saved whenever you leave the menu and
after a high score. (Making a file needs a recent MEGA65 system (Hyppo):
if saving says nothing and nothing is kept, copy `build/SAVES1.DAT`, made
by `make build/SAVES1.DAT`, into the folder yourself.) Earlier builds
called it `SAVES.DAT`: rename it `SAVES1.DAT` to keep those saves.

Then, on the MEGA65:

1. Mount the disk image as drive 8: type `MOUNT "WOLF3D.D81"` (or
   `"WOLF3D6.D81"`), or hold RESTORE for the Freezer, press `0` to choose
   drive 0's disk image, pick it, and press `F3` to resume.
2. Type `RUN "WOLF"`.

The game reads its files from the SD card's `WOLF4M65` directory (or from
the root directory, if there is no `WOLF4M65`), whichever disk image is
mounted. If a file is missing, it says which one on the screen and stops
with a red border.

Keys: cursor keys to move, CTRL to fire, ALT to strafe, SHIFT to run,
SPACE to open doors, ESC for the menu, RETURN to choose; or a joystick in
either port. The function keys are the original's: SHIFT gives the even
ones (F2 save, F3 load, F4 sound, F6 controls, F8 quick save, F9 quick
load, F10 quit). The number in the top left corner of the 3D view is the
frame rate (a development aid).

## Building

You need:

- [llvm-mos](https://github.com/llvm-mos/llvm-mos-sdk), its `bin` directory on the `PATH`
  (the tools find its `llvm-nm` and so on beside `mos-mega65-clang`; or set
  `LLVM_MOS_BIN` to that directory)
- the shareware data (`*.WL1`), and for the registered build `*.WL6`, in the repository root (git ignores them)
- for music: Bobby Prince's original MIDI files, named `NN - title.mid` as
  on the soundtrack (`03 - Get Them Before They Get You (E1M1).mid` and so
  on), in `mega65/` (git ignores them). The build converts them into
  `build/MUSIC.DAT`. Neither the MIDI files nor `MUSIC.DAT` may be passed on,
  so `make dist` leaves the music out, like the game data.
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
| `make sdcard` | the disk image and the `WOLF4M65` folder for the SD card, in `build/sdcard/` (`VERSION=wl6`: `build/sdcard-wl6/`) |
| `make dist` | `build/wolf3d-mega65-wl1.zip` (or `-wl6`): the same without the game data, the music or the sound effects (not ours to pass on), plus this README |
| `make run-xemu` | play it in Xemu (a window), on `build/sd.img` |
| `make run-wolf` | run it headless for `RUNSECS` seconds (60); keeps `wolf.png` (screenshot) and `wolf.ser` (debug output) |
| `make fps` | run the first demo and print the frame times |
| `make profile` | sample where the time goes during the demo (`FRAME=N`: to frame N) |
| `make profile-load` | the same from start-up to the first demo frame (sign-on, title, fades, loading) |
| `make profile-menu` | the same through the menus to the first frame of a new game |
| `make calltrace` | count the calls between the game's functions in the demo (`calls.txt`), for packing the code overlays (slow: every call is logged) |
| `make FRAME=N check-frame` | compare demo frame N with the original code's, pixel for pixel (writes `frame_N.png`) |
| `make test-save` | save a game and load it back, in Xemu |
| `make test-host test-proxy test-ovl test-write test-ca test-title` | the other tests |
| `make sid` | the game's AdLib music as 3-SID files (`build/sid/`) |
| `make build/SFX.DAT` | the AdLib sound effects for the fourth SID; also `build/sid/SFX_PREVIEW.sid`, every effect in turn (`vsid` it) |
| `make sid-midi` | the MIDI originals (put the `.mid` files in `mega65/`) as 3-SID files (`build/sid-midi/`) |
| `make build/MUSIC.DAT` | the game's music from the MIDI files (the targets above make it when they need it) |
| `make clean` | remove what was built |

Options:

- `VERSION=wl6` (or `wl6apo`) builds the registered game instead of the
  shareware one: put your `*.WL6` files in the repository root. The tests
  (`check-frame`, `test-*`) use the shareware data.
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
