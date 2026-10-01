# Build for the MEGA65 using llvm-mos.
CC = mos-mega65-clang
CXX = mos-mega65-clang++
CFLAGS = -Os -Wall

XEMU = xemu-xmega65
XEMUFLAGS = -headless -sleepless -fastboot

# --- Which game -------------------------------------------------------------
# VERSION=wl1 (default): the shareware episode, v1.4 (*.WL1).
# VERSION=wl6: the registered six episodes, v1.4 GT/id/Activision: the one
#   sold today (Steam, GOG) (*.WL6).
# VERSION=wl6apo: the registered six episodes, v1.4 Apogee (*.WL6).
# The data decides the game's graphics numbering, so each needs its build;
# the port's own files carry the version's number (WOLF1.OVL, WOLF6.OVL...),
# so both can share the SD card's WOLF4M65 folder.
VERSION ?= wl1
ifeq ($(VERSION),wl1)
  VFLAGS = -DUPLOAD
  EXT    = WL1
  V      = 1
else ifeq ($(VERSION),wl6)
  VFLAGS = -DGOODTIMES
  EXT    = WL6
  V      = 6
else ifeq ($(VERSION),wl6apo)
  VFLAGS =
  EXT    = WL6
  V      = 6
else
  $(error VERSION must be wl1, wl6 or wl6apo)
endif
VDEFS   = '-DM65_VSUFFIX="$(V)"'
PRG     = $(if $(filter 1,$(V)),wolf.prg,wolf$(V).prg)
D81NAME = $(if $(filter 1,$(V)),WOLF3D.D81,WOLF3D$(V).D81)

# `make` alone builds the game.
.DEFAULT_GOAL := $(PRG)

# --- Wolf4SDL translation units --------------------------------------------

WOLFDIR  = ..
# (id_vl.cpp is replaced by m65_vl.cpp, id_sd.cpp by m65_sd.cpp.)
WOLFSRCS = id_ca id_in id_pm id_us_1 id_vh wl_act1 wl_act2 wl_agent \
           wl_debug wl_draw wl_game wl_inter wl_main wl_menu wl_play \
           wl_state wl_text

# Shareware Wolf3D v1.4, all optional extras off.
WOLFFLAGS = -Os -fno-lto -I. -include m65_compat.h \
            -DVERSIONALREADYCHOSEN -DCARMACIZED $(VFLAGS) $(VDEFS) -DMEGA65 -DNDEBUG \
            $(if $(filter 0,$(SPRITECACHE)),-DM65_NOSPRITECACHE) $(if $(filter 0,$(CACHE)),-DM65_NOCACHE) \
            -Wno-c++11-narrowing -Wno-pragma-pack -Wno-switch \
            -mllvm -replexitval=never

# --- The game ---------------------------------------------------------------------
# Every game source and the overlay-able MEGA65 layers are compiled to
# assembly and split into code overlays by ovlgen; main stays resident.
# Resident as normal objects: the overlay runtime and loader, DMA and far
# primitives, file access (needed to load the overlays), the takeover.

GB        = $(if $(filter wl1,$(VERSION)),build/game,build/game-$(VERSION))
GAMECXX   = $(WOLFSRCS) m65_vl m65_sd
GAMEC     = m65_posix m65_dosw m65_huff m65_mapexp m65_sdl m65_kbd m65_test m65_trace m65_surf m65_hwdiv m65_stdio m65_libm \
            m65_video_init m65_far_alloc
GAMEASM   = $(addprefix $(GB)/,$(addsuffix .s,$(GAMECXX) $(GAMEC)))
RESIDENTC = ovl/ovl_load.c m65_far.c m65_video.c m65_dos.c m65_platform.c m65_assert.c m65_math.c
RESIDENT  = $(GB)/ovl_rt.o $(GB)/m65_prof.o $(GB)/m65_music.o $(GB)/m65_sfx.o $(GB)/m65_sidfx.o $(addprefix $(GB)/res_,$(notdir $(RESIDENTC:.c=.o)))

# make OVLTRACE=1: log every overlay call on the serial port (slow).
OVLRTFLAGS = $(if $(OVLTRACE),-Wa$(comma)-defsym$(comma)OVL_TRACE=1)
comma = ,
$(GB)/ovltrace.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(OVLTRACE)' | cmp -s - $@ || echo '$(OVLTRACE)' > $@
$(GB)/m65_sidfx.o: m65_sidfx.s
	@mkdir -p $(GB)
	$(CC) -c -fno-lto -o $@ $<

$(GB)/m65_sfx.o: m65_sfx.s
	@mkdir -p $(GB)
	$(CC) -c -fno-lto -o $@ $<

$(GB)/m65_music.o: m65_music.s
	@mkdir -p $(GB)
	$(CC) -c -fno-lto -o $@ $<

$(GB)/m65_prof.o: m65_prof.s
	@mkdir -p $(GB)
	$(CC) -c -fno-lto -o $@ $<

$(GB)/ovl_rt.o: ovl/ovl_rt.s $(GB)/ovltrace.stamp
	@mkdir -p $(GB)
	$(CC) -c -fno-lto $(OVLRTFLAGS) -o $@ $<

$(GB)/res_%.o: %.c $(M65HDRS)
	@mkdir -p $(GB)
	$(CC) -Os -fno-lto -Wall -I. -DM65_RESIDENT $(VDEFS) -c -o $@ $<

$(GB)/res_%.o: ovl/%.c
	@mkdir -p $(GB)
	$(CC) -Os -fno-lto -Wall -I. -c -o $@ $<

M65HDRS   = $(wildcard m65_*.h m65_*.hpp) SDL.h

$(GB)/%.s: $(WOLFDIR)/%.cpp $(M65HDRS) $(wildcard $(WOLFDIR)/*.h)
	@mkdir -p $(GB)
	$(CXX) $(WOLFFLAGS) -ffunction-sections -S -o $@ $<

$(GB)/m65_%.s: m65_%.cpp $(M65HDRS) $(wildcard $(WOLFDIR)/*.h)
	@mkdir -p $(GB)
	$(CXX) $(WOLFFLAGS) -I$(WOLFDIR) -ffunction-sections -S -o $@ $<

# (hand-written, and an overlay function like the compiler's)
$(GB)/m65_trace.s: m65_trace.s
	@mkdir -p $(GB)
	cp $< $@

$(GB)/m65_%.s: m65_%.c $(M65HDRS)
	@mkdir -p $(GB)
	$(CC) -Os -fno-lto -Wall -I. -ffunction-sections $(VDEFS) -S -o $@ $<

# make KEYS='{3000,0x3C},{3200,0x7F}': press keys at those times (ms) through
# the virtual-key register (see m65_kbd.c). Rebuilt whenever KEYS changes.
$(GB)/keys.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(KEYS)' | cmp -s - $@ || echo '$(KEYS)' > $@
$(GB)/m65_kbd.s: m65_kbd.c $(M65HDRS) $(GB)/keys.stamp
	$(CC) -Os -fno-lto -Wall -I. -ffunction-sections $(VDEFS) $(if $(KEYS),'-DM65_KEYSCRIPT=$(KEYS)') -S -o $@ $<
FORCE:

# make FRAME=N: stop at frame N of the demo, for a frame comparison (see
# m65_test.c and "Host reference" below).
$(GB)/frame.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(FRAME) $(PROFILE) $(BENCH)' | cmp -s - $@ || echo '$(FRAME) $(PROFILE) $(BENCH)' > $@
$(GB)/m65_test.s: m65_test.c $(M65HDRS) $(GB)/frame.stamp
	$(CC) -Os -fno-lto -Wall -I. -ffunction-sections $(VDEFS) $(if $(FRAME),-DM65_FRAMEDUMP=$(FRAME)) \
	  $(if $(PROFILE),-DM65_PROFILE) $(if $(filter all,$(PROFILE)),-DM65_PROFILE_ALL) \
	  $(if $(BENCH),-DM65_BENCH) -S -o $@ $<

# Stage 1: a probe link of the game's code alone, to learn which library
# members it pulls in. Stage 2: libsplit compiles those (bitcode) to
# assembly and decides which stay resident. Stage 3: ovlgen over the game
# and the library's overlay-able code, and the real link.

$(GB)/why.txt: $(GAMEASM) $(RESIDENT) tools/ovlgen.py
	python3 tools/ovlgen.py --out $(GB)/probe --resident-funcs main $(GAMEASM) > /dev/null
	$(CXX) -Os -fno-lto -I. -T $(GB)/probe/ovl.ld -Wl,--no-check-sections \
	  -Wl,--noinhibit-exec -Wl,--why-extract=$@ -o $(GB)/probe/probe.prg \
	  $(GB)/probe/*.ovl.s $(GB)/probe/ovl_thunks.s $(RESIDENT) 2>/dev/null || true

$(GB)/lib/overlay.txt: $(GB)/why.txt $(RESIDENT) tools/libsplit.py
	python3 tools/libsplit.py --why $< --out $(GB)/lib $(RESIDENT)

# Code runs about 16 times slower from attic RAM than from chip RAM on the
# real machine (hwtest.c), so the hot code goes in overlays that run from
# chip RAM ($20000-$3FFFF, 16 slots): first the functions in hot.txt (by time
# spent in the demo: make hotlist), then those in hot-ui.txt (loading, fades,
# menus: by hand, from make profile-load), then the files in HOTFILES, in order.
# Among those overlays, functions that call each other often go together
# (calls.txt, from make calltrace): a call across overlays remaps the window.
CHIPSLOTS = 16
HOTFILES  = m65_vl,m65_surf,id_vh,id_us_1,m65_kbd,m65_sdl,id_in,id_pm,wl_play,wl_draw,\
            wl_agent,wl_act1,wl_state,wl_act2,divmod_cc_obj,wl_game

# make THUNKALL=1: every call through a thunk (for make calltrace).
$(GB)/thunkall.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(THUNKALL)' | cmp -s - $@ || echo '$(THUNKALL)' > $@

$(GB)/ovl.ld: $(GAMEASM) $(GB)/lib/overlay.txt tools/ovlgen.py hot.txt hot-ui.txt $(GB)/thunkall.stamp \
              $(wildcard calls.txt)
	rm -f $(GB)/*.ovl.s
	python3 tools/ovlgen.py --out $(GB) --resident-funcs main $(if $(THUNKALL),--thunk-all) \
	  $(if $(wildcard calls.txt),--calls calls.txt) \
	  --chip-slots $(CHIPSLOTS) --hot hot.txt,hot-ui.txt --hot-files '$(subst $(eval) ,,$(HOTFILES))' \
	  --extern-objs $$(echo $(RESIDENT) $$(sed 's/\.s$$/.o/' $(GB)/lib/resident.txt) | tr ' ' ',') \
	  $(GAMEASM) $$(cat $(GB)/lib/overlay.txt)

$(PRG): $(GB)/ovl.ld $(RESIDENT)
	$(CXX) -Os -fno-lto -I. -T $(GB)/ovl.ld -Wl,--no-check-sections -o $@ \
	  $(GB)/*.ovl.s $(GB)/ovl_thunks.s $(RESIDENT) $$(cat $(GB)/lib/resident.txt)
	python3 tools/ovlpack.py $@.elf $$(grep -c '^overlay' $(GB)/plan.txt) $(GB)/WOLF$(V).OVL
	llvm-objcopy -O binary --only-section=.rodata_far --only-section=.midtext --only-section=.data $@.elf $(GB)/WOLF$(V).DAT

# --- Tests that need an SD card ---------------------------------------------
# Xemu boots the real Hyppo firmware from a copy of your Xemu SD image; the
# files in testsd/ are copied into the copy's root directory.

XEMUDIR ?= $(HOME)/.local/share/xemu-lgb/mega65
SDIMG    = build/sd.img

GAMEDATA = $(wildcard $(WOLFDIR)/*.$(EXT))

# Files the MEGA65 build adds to the game data (see SignonScreen in wl_main).
build/SIGNON.BIN: tools/extract_signon.py $(WOLFDIR)/signon.cpp
	@mkdir -p build
	python3 tools/extract_signon.py $(WOLFDIR)/signon.cpp $@

build/TABLES.BIN: tools/gen_tables.py
	@mkdir -p build
	python3 tools/gen_tables.py $@

PORTDATA = build/SIGNON.BIN build/TABLES.BIN

# The music, from the user's MIDI files (mega65/*.mid, not in the repository
# and not to be distributed): build/MUSIC.DAT, only when they are there. The
# game plays without it, silently.
# (The file names have spaces, which make cannot take as prerequisites: the
# shell looks for them, and for any newer than MUSIC.DAT.)
MIDIPRESENT := $(shell ls *.mid >/dev/null 2>&1 && echo yes)
MIDINEWER := $(shell [ -e build/MUSIC.DAT ] && find . -maxdepth 1 -name '*.mid' -newer build/MUSIC.DAT | head -1)
MUSICDATA = $(if $(MIDIPRESENT),build/MUSIC.DAT)
build/MUSIC.DAT: tools/midi2sid.py tools/sidpack.py $(if $(MIDINEWER),FORCE)
	@mkdir -p build
	python3 tools/midi2sid.py --game $@ .

# The AdLib sound effects for the fourth SID, from the game data (like the
# music: built from the user's files, not to be passed on).
# (Only when the version's data is there, like the music.)
SFXDATA = $(if $(wildcard $(WOLFDIR)/AUDIOT.$(EXT)),build/SFX$(V).DAT)
build/SFX$(V).DAT: tools/adlib2sid.py tools/imf2sid.py $(WOLFDIR)/AUDIOHED.$(EXT) $(WOLFDIR)/AUDIOT.$(EXT)
	@mkdir -p build/sid
	python3 tools/adlib2sid.py $(WOLFDIR)/AUDIOHED.$(EXT) $(WOLFDIR)/AUDIOT.$(EXT) $@ \
	  --preview build/sid/SFX_PREVIEW.sid --model $(SIDMODEL)

# What the game loads besides the game data (copied onto the SD card).
GAMEFILES = $(GB)/WOLF$(V).OVL $(GB)/WOLF$(V).DAT $(PORTDATA) $(MUSICDATA) $(SFXDATA)

# The save games and the config file, in SAVES.DAT (m65_posix.c): 11 slots
# of 32KB. The game makes it (empty) where there is none, so it is not in
# build/sdcard/: a new version copied onto the card leaves the saves alone.
# The tests start from an empty one (so that runs do not depend on what a
# previous run saved), except test-save, which has the game make it; for a
# MEGA65 whose Hyppo cannot make files, copy build/SAVES.DAT by hand.
SAVEFILES = build/SAVES$(V).DAT
$(SAVEFILES):
	@mkdir -p build
	head -c $$((11 * 32768)) /dev/zero > $@

# The game's directory on the SD card (m65_posix.c: m65_dos_subdir).
SDDIR = s:/WOLF4M65/

# The test programs read from the root directory, the game from WOLF4M65.
$(SDIMG): $(XEMUDIR)/mega65.img $(wildcard testsd/*) $(GAMEDATA) $(PORTDATA) $(SAVEFILES)
	@mkdir -p build
	cp --sparse=always $< $@
	printf 'mtools_skip_check=1\ndrive s: file="$(abspath $@)" offset=1048576\n' > build/mtoolsrc
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o testsd/* $(GAMEDATA) $(PORTDATA) s:
	MTOOLSRC=$(abspath build/mtoolsrc) mmd $(patsubst %/,%,$(SDDIR))
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEDATA) $(PORTDATA) $(SAVEFILES) $(SDDIR)

# Hardware timing test (see hwtest.c): on the D81 as HWTEST.
hwtest.prg: hwtest.c m65_video.c m65_video.h m65_math.c m65_hwdiv.c
	$(CC) $(CFLAGS) -fno-lto -I. -Wl,--allow-multiple-definition -o $@ \
	  hwtest.c m65_video.c m65_math.c m65_hwdiv.c   # (LTO: clang crashes on the asm; ours before libc's)

# --- Overlay test ------------------------------------------------------------
# ov1.c and ov2.c each become an overlay (--per-file); main.c and the runtime
# stay resident. Checks thunks, pointers to statics, recursion, tail calls and
# overlay <-> overlay <-> resident chains.

OT = build/ovltest

$(OT)/%.s: ovltest/%.c ovltest/ovltest.h
	@mkdir -p $(OT)
	$(CC) -Os -fno-lto -ffunction-sections -S -o $@ $<

$(OT)/main.o: ovltest/main.c ovltest/ovltest.h
	@mkdir -p $(OT)
	$(CC) -Os -fno-lto -Wall -I. -c -o $@ $<

$(OT)/ovl.ld: $(OT)/ov1.s $(OT)/ov2.s $(OT)/main.o tools/ovlgen.py
	python3 tools/ovlgen.py --out $(OT) --per-file --chip-slots 1 --hot-files ov1 \
	  --extern-objs $(OT)/main.o \
	  $(OT)/ov1.s $(OT)/ov2.s

ovltest.prg: $(OT)/ovl.ld $(OT)/main.o ovl/ovl_rt.s ovl/ovl_load.c m65_dos.c m65_video.c m65_platform.c m65_far.c
	$(CC) -Os -fno-lto -Wall $(VDEFS) -T $(OT)/ovl.ld -Wl,--no-check-sections -o $@ $(OT)/main.o \
	  $(OT)/ov1.ovl.s $(OT)/ov2.ovl.s $(OT)/ovl_thunks.s ovl/ovl_rt.s \
	  ovl/ovl_load.c m65_dos.c m65_video.c m65_platform.c m65_far.c
	python3 tools/ovlpack.py $@.elf $$(grep -c '^overlay' $(OT)/plan.txt) $(OT)/OVLTEST.OVL
	llvm-objcopy -O binary --only-section=.rodata_far --only-section=.midtext --only-section=.data $@.elf $(OT)/OVLTEST.DAT

test-ovl: ovltest.prg $(SDIMG)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(OT)/OVLTEST.OVL $(OT)/OVLTEST.DAT s:
	python3 tools/xrun.py 60 ovltest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg ovltest.prg -dumpmem ovltest.mem
	python3 tools/check_ovltest.py ovltest.mem

# --- Equivalence tests for changes to the game code --------------------------
# Old code (from git) against new, run in the llvm-mos simulator (16-bit int).

build/hosttest/shifttest.cpp: tools/make_shifttest.py $(WOLFDIR)/wl_play.cpp
	@mkdir -p build/hosttest
	python3 tools/make_shifttest.py > $@

build/hosttest/%.sim: build/hosttest/%.cpp
	mos-sim-clang++ -Os -I$(WOLFDIR) -o $@ $<

build/hosttest/libmtest.c: tools/gen_libmtest.py
	@mkdir -p build/hosttest
	python3 tools/gen_libmtest.py $@

build/hosttest/libmtest.sim: build/hosttest/libmtest.c m65_libm.c
	mos-sim-clang -Os -o $@ $^

test-host: build/hosttest/shifttest.sim build/hosttest/libmtest.sim
	mos-sim build/hosttest/shifttest.sim
	mos-sim build/hosttest/libmtest.sim

# --- Cache test: the real id_ca.cpp --------------------------------------------
# (Also the far memory layer, file loading and Huffman decoding: catest checks
# every graphics chunk, map and VSWAP page against the original code's.)

build/huffref.h: tools/gen_huffref.py $(GAMEDATA)
	@mkdir -p build
	python3 tools/gen_huffref.py $@

build/mapref.h: tools/gen_mapref.py $(GAMEDATA)
	@mkdir -p build
	python3 tools/gen_mapref.py $@

# The MEGA65 layer's C files, compiled as C (-x c, after the C++ files) as
# in the game: clang++ would take them for C++.
M65LIB = m65_assert.c m65_libm.c m65_far.c m65_far_alloc.c m65_huff.c m65_mapexp.c m65_posix.c m65_dosw.c \
         m65_dos.c m65_video.c m65_video_init.c

catest.prg: catest.cpp build/huffref.h build/mapref.h $(WOLFDIR)/id_ca.cpp $(WOLFDIR)/id_pm.cpp $(M65LIB)
	$(CXX) $(WOLFFLAGS) -o $@ catest.cpp $(WOLFDIR)/id_ca.cpp $(WOLFDIR)/id_pm.cpp -x c $(M65LIB)

test-ca: catest.prg $(SDIMG)
	python3 tools/xrun.py 600 catest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg catest.prg -dumpmem catest.mem
	@tr -d '\r' < catest.ser | grep -a -A1 '^QUIT:' || true
	python3 tools/check_catest.py catest.mem build/huffref.h build/mapref.h $(WOLFDIR)/VSWAP.WL1

# --- Title test: id_ca + id_vh + the MEGA65 video layer ----------------------------

M65VID = m65_sdl.c m65_kbd.c m65_surf.c

titletest.prg: titletest.cpp build/huffref.h m65_vl.cpp $(WOLFDIR)/id_ca.cpp $(WOLFDIR)/id_vh.cpp $(M65LIB) $(M65VID)
	$(CXX) $(WOLFFLAGS) -I$(WOLFDIR) -o $@ titletest.cpp m65_vl.cpp $(WOLFDIR)/id_ca.cpp \
	  $(WOLFDIR)/id_vh.cpp -x c $(M65LIB) $(M65VID)

test-title: titletest.prg $(SDIMG)
	python3 tools/xrun.py 120 titletest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg titletest.prg -dumpmem titletest.mem -screenshot titletest.png
	@tr -d '\r' < titletest.ser | grep -a -A1 '^QUIT:' || true
	python3 tools/check_titletest.py titletest.mem build/huffref.h build/SIGNON.BIN \
	  build/TITLE.RAW $(WOLFDIR)/wolfpal.inc titletest.png

# --- Far proxy types (map planes, actorat) ----------------------------------------

proxytest.prg: proxytest.cpp m65_farref.hpp m65_fararray.hpp m65_fargrid.hpp $(M65LIB)
	$(CXX) $(WOLFFLAGS) -o $@ proxytest.cpp -x c $(M65LIB)

test-proxy: proxytest.prg
	python3 tools/xrun.py 60 proxytest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -prg proxytest.prg -dumpmem proxytest.mem
	python3 tools/check_proxytest.py proxytest.mem

# Writing (m65_dosw.c) and the WOLF4M65 directory: a 1024-byte TESTW.BIN is
# overwritten there and read back.
writetest.prg: writetest.c m65_dos.c m65_dosw.c m65_dos.h m65_video.c m65_video.h
	$(CC) $(CFLAGS) -fno-lto -o $@ writetest.c m65_dos.c m65_dosw.c m65_video.c

test-write: writetest.prg $(SDIMG)
	head -c 1024 /dev/zero > build/TESTW.BIN
	MTOOLSRC=$(abspath build/mtoolsrc) mmd s:/WOLF4M65 2>/dev/null || true
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o build/TESTW.BIN testsd/test.bin s:/WOLF4M65/
	python3 tools/xrun.py 90 writetest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg writetest.prg -dumpmem writetest.mem
	python3 tools/check_writetest.py writetest.mem

# Run the whole game headless for RUNSECS seconds; keeps the debug serial
# output (wolf.ser), a memory dump and a screenshot of where it got to.
RUNSECS ?= 60
run-wolf: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SAVEFILES) $(SDDIR)
	-python3 tools/xrun.py $(RUNSECS) wolf.ser WOLF-NEVER -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG) -dumpmem wolf.mem -screenshot wolf.png
	@tr -d '\r' < wolf.ser | tail -20

# --- For the real machine --------------------------------------------------
# build/sdcard/: what goes in the root directory of the MEGA65's SD card:
# WOLF3D.D81 (a disk image holding the program, WOLF), and the folder
# WOLF4M65 with the files the game loads itself (see README.md).
SDCARD = $(if $(filter wl1,$(VERSION)),build/sdcard,build/sdcard-$(VERSION))
sdcard: $(PRG) hwtest.prg $(PORTDATA) $(MUSICDATA) $(SFXDATA)
	rm -rf $(SDCARD)
	mkdir -p $(SDCARD)/WOLF4M65
	cp $(GAMEFILES) $(GAMEDATA) $(SDCARD)/WOLF4M65/
	c1541 -silent -format "wolfenstein 3d,w3" d81 $(SDCARD)/$(D81NAME) -write $(PRG) wolf \
	  -write hwtest.prg hwtest
	@echo; echo "Copy $(D81NAME) and the folder WOLF4M65 from $(SDCARD)/ to the SD card's root directory."
	@echo "(The game makes SAVES$(V).DAT, the save games and settings, there itself.)"
	@ls -l $(SDCARD) $(SDCARD)/WOLF4M65

# build/wolf3d-mega65-VERSION.zip: the same without the game data (*.WL1,
# *.WL6) and without what is made from it or from the MIDI files (SFX*.DAT,
# MUSIC.DAT), for passing around: whoever installs it adds their own data.
DISTZIP = build/wolf3d-mega65-$(VERSION).zip
dist: sdcard
	rm -f $(DISTZIP)
	cd $(SDCARD) && zip -q ../$(notdir $(DISTZIP)) $(D81NAME) $(addprefix WOLF4M65/,WOLF$(V).OVL WOLF$(V).DAT \
	  SIGNON.BIN TABLES.BIN)
	cp README.md build/README-mega65.md && cd build && zip -q $(notdir $(DISTZIP)) README-mega65.md
	@ls -l $(DISTZIP)

# Speed: run the first demo for RUNSECS and show the frame times it reports
# (m65_test.c). Emulated time, so the host's speed does not matter; Xemu's
# timing is close to, but not exactly, the real machine's.
fps: KEYS = {6000,0x3C},{6300,0x7F}
fps: RUNSECS = 150
fps: run-wolf
	@tr -d '\r' < wolf.ser | grep -a '^FPS' || true

# Where the time goes: sample the CPU from demo frame 1 to FRAME (default
# 60) and list the busiest functions (m65_prof.s, tools/profile.py).
profile: KEYS = {6000,0x3C},{6300,0x7F}
profile: PROFILE = 1
profile: FRAME ?= 60
profile: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SAVEFILES) $(SDDIR)
	python3 tools/xrun.py 300 profile.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG) -dumpmem profile.mem
	python3 tools/profile.py $(PRG).elf profile.mem

# Where the time goes from start-up to the first demo frame: the sign-on,
# title, fades and the demo's level loading (m65_test.c).
profile-load: KEYS = {6000,0x3C},{6300,0x7F}
profile-load: PROFILE = all
profile-load: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SAVEFILES) $(SDDIR)
	python3 tools/xrun.py 400 profile.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG) -dumpmem profile.mem -screenshot profile.png
	python3 tools/profile.py $(PRG).elf profile.mem 60

# The same through the menus to a new game: SPACE for the sign-on's "Press a
# key", ESC to skip the PG-13 screen, ESC at the title for the menu, down four
# times from "Read This!" (wrapping) to "New Game", then RETURN for it, the
# episode and the difficulty; up to the game's first frame. (The serial log,
# profile.ser, shows each scripted key as K and its scancode.)
profile-menu: KEYS = {6000,0x3C},{6300,0x7F},{14000,0x47},{14300,0x7F},{20000,0x47},{20300,0x7F},{24000,0x07},{24300,0x7F},{25000,0x07},{25300,0x7F},{26000,0x07},{26300,0x7F},{27000,0x07},{27300,0x7F},{30000,0x01},{30300,0x7F},{34000,0x01},{34300,0x7F},{38000,0x01},{38300,0x7F}
profile-menu: PROFILE = all
profile-menu: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SAVEFILES) $(SDDIR)
	python3 tools/xrun.py 400 profile.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG) -dumpmem profile.mem -screenshot profile.png
	python3 tools/profile.py $(PRG).elf profile.mem 60

# Saving and loading (m65_posix.c), with no SAVES.DAT to start with (the game
# makes it): a new game, ESC, down four times to
# "Save Game", RETURN, RETURN for slot 0, the name "A", RETURN; then ESC
# twice (the main menu's "quit?", answered no), down nine times (wrapping)
# to "Load Game", RETURN, RETURN for slot 0. The serial log shows each file
# written (SAVED) and each save read; tools/check_savetest.py checks those and
# SAVES.DAT on the SD image. (The run ends when the script has run out, at
# 92s of the machine's time: KEYS-DONE, m65_kbd.c; not on a timer of the
# host's, which made the test depend on how busy the host was.)
test-save: KEYS = {6000,0x3C},{6300,0x7F},{14000,0x47},{14300,0x7F},{20000,0x47},{20300,0x7F},{24000,0x07},{24300,0x7F},{25000,0x07},{25300,0x7F},{26000,0x07},{26300,0x7F},{27000,0x07},{27300,0x7F},{30000,0x01},{30300,0x7F},{34000,0x01},{34300,0x7F},{38000,0x01},{38300,0x7F},{44000,0x47},{44300,0x7F},{47000,0x07},{47300,0x7F},{48000,0x07},{48300,0x7F},{49000,0x07},{49300,0x7F},{50000,0x07},{50300,0x7F},{52000,0x01},{52300,0x7F},{55000,0x01},{55300,0x7F},{57000,0x0A},{57300,0x7F},{58000,0x01},{58300,0x7F},{62000,0x47},{62300,0x7F},{66000,0x47},{66300,0x7F},{69000,0x07},{69300,0x7F},{70000,0x07},{70300,0x7F},{71000,0x07},{71300,0x7F},{72000,0x07},{72300,0x7F},{73000,0x07},{73300,0x7F},{74000,0x07},{74300,0x7F},{75000,0x07},{75300,0x7F},{76000,0x07},{76300,0x7F},{77000,0x07},{77300,0x7F},{80000,0x01},{80300,0x7F},{83000,0x01},{83300,0x7F},{92000,0x7F}
test-save: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SDDIR)
	-MTOOLSRC=$(abspath build/mtoolsrc) mdel $(SDDIR)SAVES$(V).DAT
	python3 tools/xrun.py 90 wolf.ser KEYS-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG) -dumpmem wolf.mem -screenshot wolf.png
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(SDDIR)SAVES$(V).DAT build/SAVES$(V).GOT
	python3 tools/check_savetest.py wolf.ser build/SAVES$(V).GOT

# calls.txt: the calls between the game's functions in demo frames 101-200,
# counted (tools/ovlcalls.py), from a build that routes every call through
# the overlay runtime and logs it (THUNKALL, OVLTRACE: slow). ovlgen packs
# functions that call each other often into the same overlay.
calltrace: THUNKALL = 1
calltrace: OVLTRACE = 1
calltrace: FRAME = 201
calltrace: KEYS = {6000,0x3C},{6300,0x7F}
calltrace: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SAVEFILES) $(SDDIR)
	python3 tools/xrun.py 3000 build/calltrace.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG)
	python3 tools/ovlcalls.py build/calltrace.ser $(PRG).elf 101 200 calls.txt

# hot.txt from a profile of the first 1000 demo frames.
hotlist: FRAME = 1000
hotlist: profile
	python3 tools/profile.py $(PRG).elf profile.mem 0 --hot hot.txt

# Play it in Xemu (a window), on the SD image that has the game's files:
# build/sd.img (a copy of your Xemu image; yours is left alone).
run-xemu: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SDDIR)
	$(XEMU) -sdimg $(SDIMG) -prg $(PRG)

# --- Music preview -------------------------------------------------------------
# The game's AdLib music as 3-SID PSID files (build/sid/*.sid; play them with
# VICE's vsid). See tools/imf2sid.py. SIDMODEL=6581 for the older chip.
SIDMODEL ?= 8580
sid: tools/imf2sid.py
	python3 tools/imf2sid.py --model $(SIDMODEL) $(WOLFDIR)/AUDIOHED.WL1 $(WOLFDIR)/AUDIOT.WL1 build/sid

# From Bobby Prince's MIDI originals (put the .mid files in this directory;
# they are not part of the repository): build/sid-midi/*.sid. See
# tools/midi2sid.py.
sid-midi: tools/midi2sid.py tools/sidpack.py
	python3 tools/midi2sid.py --model $(SIDMODEL) build/sid-midi *.mid

# --- Host reference ---------------------------------------------------------
# The original game code (no MEGA65 changes) built for the host against a
# RAM-only SDL (host/host_sdl.cpp): silent, invisible, with a virtual clock.
# Demos advance a fixed number of tics per frame, so demo frame N is the same
# everywhere: make FRAME=N check-frame compares the MEGA65's frame N with
# the host's, pixel for pixel.

HB       = build/host
HOSTCXX  = g++
HOSTFLAGS = -O1 -g -w -fno-strict-aliasing -DVERSIONALREADYCHOSEN -DCARMACIZED \
            -DUPLOAD -DNDEBUG -DFRAMEDUMP -Ihost
HOSTSRCS = $(addprefix $(WOLFDIR)/,$(addsuffix .cpp,$(WOLFSRCS) id_vl signon)) \
           host/host_sd.cpp host/host_sdl.cpp host/host_frame.cpp

$(HB)/wolf-host: $(HOSTSRCS) $(wildcard $(WOLFDIR)/*.h) SDL.h $(wildcard host/*.h)
	@mkdir -p $(HB)
	$(HOSTCXX) $(HOSTFLAGS) -o $@ $(HOSTSRCS) -lm

# The host frames for FRAMES="n,m,..." in build/host/run (the game files
# under the lower-case names the original code opens).
host-frames: $(HB)/wolf-host
	@mkdir -p $(HB)/run
	@for f in $(GAMEDATA); do b=$$(basename $$f); ln -sf $(abspath $$f) $(HB)/run/$$(echo $$b | tr A-Z a-z); done
	rm -rf $(HB)/run/.wolf4sdl $(HB)/run/frame_*.raw
	cd $(HB)/run && HOME=$(abspath $(HB)/run) FRAMES='$(FRAMES)' ../wolf-host --res 320 200

# The MEGA65's demo frame FRAME (it stops there), compared with the host's.
# (SPACE at 6s answers the sign-on screen's "Press a key", as on the host.)
check-frame: KEYS = {6000,0x3C},{6300,0x7F}
check-frame: $(PRG) $(SDIMG) $(MUSICDATA) $(SFXDATA) $(HB)/wolf-host
	$(MAKE) host-frames FRAMES=$(FRAME)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GAMEFILES) $(SAVEFILES) $(SDDIR)
	python3 tools/xrun.py 300 frame.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg $(PRG) -dumpmem frame.mem
	python3 tools/check_frame.py frame.mem $(HB)/run/frame_$$(printf %04d $(FRAME)).raw \
	  $(WOLFDIR)/wolfpal.inc frame_$(FRAME).png

# Run a PRG headless for a while and save a screenshot: make shot PRG=hwtest.prg
shot: $(PRG)
	timeout 60 $(XEMU) $(XEMUFLAGS) -prg $(PRG) -screenshot $(PRG:.prg=.png) -prgexit

clean:
	rm -f *.prg *.elf *.o *.png *.mem *.log *.ser; rm -rf build

.PHONY: clean shot sid sid-midi fps profile profile-load profile-menu calltrace hotlist sdcard dist run-wolf run-xemu host-frames check-frame test-write test-save test-ovl test-host test-ca test-title test-proxy
