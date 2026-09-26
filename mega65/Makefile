# Build for the MEGA65 using llvm-mos.
CC = mos-mega65-clang
CXX = mos-mega65-clang++
CFLAGS = -Os -Wall

XEMU = xemu-xmega65
XEMUFLAGS = -headless -sleepless -fastboot

# --- Wolf4SDL translation units --------------------------------------------
# Not linkable yet: id_sd.cpp (sound) is replaced by a stub, and the libc
# gaps (m65_libc.c) and SDL glue (m65_sdl.c) are still to be written.
# -fno-lto so that `make size` reports real machine-code sizes.

WOLFDIR  = ..
# (id_vl.cpp is replaced by m65_vl.cpp on the MEGA65.)
WOLFSRCS = id_ca id_in id_pm id_us_1 id_vh wl_act1 wl_act2 wl_agent \
           wl_debug wl_draw wl_game wl_inter wl_main wl_menu wl_play \
           wl_state wl_text
WOLFOBJS = $(addprefix obj/,$(addsuffix .o,$(WOLFSRCS))) obj/m65_vl.o

# Shareware Wolf3D v1.4, all optional extras off.
WOLFFLAGS = -Os -fno-lto -I. -include m65_compat.h \
            -DVERSIONALREADYCHOSEN -DCARMACIZED -DUPLOAD -DMEGA65 -DNDEBUG \
            -Wno-c++11-narrowing -Wno-pragma-pack -Wno-switch \
            -mllvm -replexitval=never

obj/%.o: $(WOLFDIR)/%.cpp m65_compat.h SDL.h
	@mkdir -p obj
	$(CXX) $(WOLFFLAGS) -c $< -o $@

obj/m65_vl.o: m65_vl.cpp m65_compat.h SDL.h
	@mkdir -p obj
	$(CXX) $(WOLFFLAGS) -I$(WOLFDIR) -c $< -o $@

wolf-objs: $(WOLFOBJS)

# --- The game ---------------------------------------------------------------------
# Every game source and the overlay-able MEGA65 layers are compiled to
# assembly and split into code overlays by ovlgen; main stays resident.
# Resident as normal objects: the overlay runtime and loader, DMA and far
# primitives, file access (needed to load the overlays), the takeover.

GB        = build/game
GAMECXX   = $(WOLFSRCS) m65_vl m65_sd
GAMEC     = m65_posix m65_huff m65_mapexp m65_sdl m65_kbd m65_test m65_surf m65_stdio m65_libm \
            m65_video_init m65_far_alloc
GAMEASM   = $(addprefix $(GB)/,$(addsuffix .s,$(GAMECXX) $(GAMEC)))
RESIDENTC = ovl/ovl_load.c m65_far.c m65_video.c m65_dos.c m65_platform.c m65_assert.c m65_math.c
RESIDENT  = $(GB)/ovl_rt.o $(GB)/m65_prof.o $(GB)/m65_draw.o $(addprefix $(GB)/res_,$(notdir $(RESIDENTC:.c=.o)))

# make OVLTRACE=1: log every overlay call on the serial port (slow).
OVLRTFLAGS = $(if $(OVLTRACE),-Wa$(comma)-defsym$(comma)OVL_TRACE=1)
comma = ,
$(GB)/ovltrace.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(OVLTRACE)' | cmp -s - $@ || echo '$(OVLTRACE)' > $@
$(GB)/m65_draw.o: m65_draw.s
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
	$(CC) -Os -fno-lto -Wall -I. -c -o $@ $<

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

$(GB)/m65_%.s: m65_%.c $(M65HDRS)
	@mkdir -p $(GB)
	$(CC) -Os -fno-lto -Wall -I. -ffunction-sections -S -o $@ $<

# make KEYS='{3000,0x3C},{3200,0x7F}': press keys at those times (ms) through
# the virtual-key register (see m65_kbd.c). Rebuilt whenever KEYS changes.
$(GB)/keys.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(KEYS)' | cmp -s - $@ || echo '$(KEYS)' > $@
$(GB)/m65_kbd.s: m65_kbd.c $(M65HDRS) $(GB)/keys.stamp
	$(CC) -Os -fno-lto -Wall -I. -ffunction-sections $(if $(KEYS),'-DM65_KEYSCRIPT=$(KEYS)') -S -o $@ $<
FORCE:

# make FRAME=N: stop at frame N of the demo, for a frame comparison (see
# m65_test.c and "Host reference" below).
$(GB)/frame.stamp: FORCE
	@mkdir -p $(GB)
	@echo '$(FRAME) $(PROFILE)' | cmp -s - $@ || echo '$(FRAME) $(PROFILE)' > $@
$(GB)/m65_test.s: m65_test.c $(M65HDRS) $(GB)/frame.stamp
	$(CC) -Os -fno-lto -Wall -I. -ffunction-sections $(if $(FRAME),-DM65_FRAMEDUMP=$(FRAME)) \
	  $(if $(PROFILE),-DM65_PROFILE) -S -o $@ $<

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

$(GB)/ovl.ld: $(GAMEASM) $(GB)/lib/overlay.txt tools/ovlgen.py
	rm -f $(GB)/*.ovl.s
	python3 tools/ovlgen.py --out $(GB) --resident-funcs main \
	  --extern-objs $$(echo $(RESIDENT) $$(sed 's/\.s$$/.o/' $(GB)/lib/resident.txt) | tr ' ' ',') \
	  $(GAMEASM) $$(cat $(GB)/lib/overlay.txt)

wolf.prg: $(GB)/ovl.ld $(RESIDENT)
	$(CXX) -Os -fno-lto -I. -T $(GB)/ovl.ld -Wl,--no-check-sections -o $@ \
	  $(GB)/*.ovl.s $(GB)/ovl_thunks.s $(RESIDENT) $$(cat $(GB)/lib/resident.txt)
	python3 tools/ovlpack.py $@.elf $$(grep -c '^overlay' $(GB)/plan.txt) $(GB)/WOLF.OVL
	llvm-objcopy -O binary --only-section=.rodata_far --only-section=.data $@.elf $(GB)/WOLF.DAT

# --- Tests that need an SD card ---------------------------------------------
# Xemu boots the real Hyppo firmware from a copy of your Xemu SD image; the
# files in testsd/ are copied into the copy's root directory.

XEMUDIR ?= $(HOME)/.local/share/xemu-lgb/mega65
SDIMG    = build/sd.img

GAMEDATA = $(wildcard $(WOLFDIR)/*.WL1)

# Files the MEGA65 build adds to the game data (see SignonScreen in wl_main).
build/SIGNON.BIN: tools/extract_signon.py $(WOLFDIR)/signon.cpp
	@mkdir -p build
	python3 tools/extract_signon.py $(WOLFDIR)/signon.cpp $@

build/TABLES.BIN: tools/gen_tables.py
	@mkdir -p build
	python3 tools/gen_tables.py $@

PORTDATA = build/SIGNON.BIN build/TABLES.BIN

$(SDIMG): $(XEMUDIR)/mega65.img $(wildcard testsd/*) $(GAMEDATA) $(PORTDATA)
	@mkdir -p build
	cp --sparse=always $< $@
	printf 'mtools_skip_check=1\ndrive s: file="$(abspath $@)" offset=1048576\n' > build/mtoolsrc
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o testsd/* $(GAMEDATA) $(PORTDATA) s:

dostest.prg: dostest.c m65_dos.c m65_dos.h m65_video.c m65_video.h
	$(CC) $(CFLAGS) -o $@ dostest.c m65_dos.c m65_video.c

loadtest.prg: loadtest.c m65_dos.c m65_dos.h m65_video.c m65_video.h
	$(CC) $(CFLAGS) -o $@ loadtest.c m65_dos.c m65_video.c

test-load: loadtest.prg $(SDIMG)
	python3 tools/xrun.py 300 loadtest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg loadtest.prg -dumpmem loadtest.mem
	python3 tools/check_loadtest.py loadtest.mem $(WOLFDIR)/VSWAP.WL1

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
	python3 tools/ovlgen.py --out $(OT) --per-file --extern-objs $(OT)/main.o \
	  $(OT)/ov1.s $(OT)/ov2.s

ovltest.prg: $(OT)/ovl.ld $(OT)/main.o ovl/ovl_rt.s ovl/ovl_load.c m65_dos.c m65_video.c m65_platform.c
	$(CC) -Os -fno-lto -Wall -T $(OT)/ovl.ld -Wl,--no-check-sections -o $@ $(OT)/main.o \
	  $(OT)/ov1.ovl.s $(OT)/ov2.ovl.s $(OT)/ovl_thunks.s ovl/ovl_rt.s \
	  ovl/ovl_load.c m65_dos.c m65_video.c m65_platform.c
	python3 tools/ovlpack.py $@.elf $$(grep -c '^overlay' $(OT)/plan.txt) $(OT)/OVLTEST.OVL
	llvm-objcopy -O binary --only-section=.rodata_far --only-section=.data $@.elf $(OT)/OVLTEST.DAT

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

# --- Far memory / Huffman test ------------------------------------------------

build/huffref.h: tools/gen_huffref.py $(GAMEDATA)
	@mkdir -p build
	python3 tools/gen_huffref.py $@

hufftest.prg: hufftest.c build/huffref.h m65_huff.c m65_huff.h m65_far.c m65_far_alloc.c m65_far.h m65_dos.c m65_video.c
	$(CC) $(CFLAGS) -o $@ hufftest.c m65_huff.c m65_far.c m65_far_alloc.c m65_dos.c m65_video.c

test-huff: hufftest.prg $(SDIMG)
	python3 tools/xrun.py 600 hufftest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg hufftest.prg -dumpmem hufftest.mem
	python3 tools/check_hufftest.py hufftest.mem build/huffref.h

# --- Cache test: the real id_ca.cpp --------------------------------------------

build/mapref.h: tools/gen_mapref.py $(GAMEDATA)
	@mkdir -p build
	python3 tools/gen_mapref.py $@

M65LIB = m65_assert.c m65_libm.c m65_far.c m65_far_alloc.c m65_huff.c m65_mapexp.c m65_posix.c \
         m65_dos.c m65_video.c m65_video_init.c

catest.prg: catest.cpp build/huffref.h build/mapref.h $(WOLFDIR)/id_ca.cpp $(WOLFDIR)/id_pm.cpp $(M65LIB)
	$(CXX) $(WOLFFLAGS) -o $@ catest.cpp $(WOLFDIR)/id_ca.cpp $(WOLFDIR)/id_pm.cpp $(M65LIB)

test-ca: catest.prg $(SDIMG)
	python3 tools/xrun.py 600 catest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg catest.prg -dumpmem catest.mem
	@tr -d '\r' < catest.ser | grep -a -A1 '^QUIT:' || true
	python3 tools/check_catest.py catest.mem build/huffref.h build/mapref.h $(WOLFDIR)/VSWAP.WL1

# --- Title test: id_ca + id_vh + the MEGA65 video layer ----------------------------

M65VID = m65_sdl.c m65_kbd.c m65_surf.c

titletest.prg: titletest.cpp build/huffref.h m65_vl.cpp $(WOLFDIR)/id_ca.cpp $(WOLFDIR)/id_vh.cpp $(M65LIB) $(M65VID)
	$(CXX) $(WOLFFLAGS) -I$(WOLFDIR) -o $@ titletest.cpp m65_vl.cpp $(WOLFDIR)/id_ca.cpp \
	  $(WOLFDIR)/id_vh.cpp $(M65LIB) $(M65VID)

test-title: titletest.prg $(SDIMG)
	python3 tools/xrun.py 120 titletest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg titletest.prg -dumpmem titletest.mem -screenshot titletest.png
	@tr -d '\r' < titletest.ser | grep -a -A1 '^QUIT:' || true
	python3 tools/check_titletest.py titletest.mem build/huffref.h build/SIGNON.BIN \
	  build/TITLE.RAW $(WOLFDIR)/wolfpal.inc titletest.png

# --- Far proxy types (map planes, actorat) ----------------------------------------

proxytest.prg: proxytest.cpp m65_farword.hpp m65_fargrid.hpp $(M65LIB)
	$(CXX) $(WOLFFLAGS) -o $@ proxytest.cpp $(M65LIB)

test-proxy: proxytest.prg
	python3 tools/xrun.py 60 proxytest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -prg proxytest.prg -dumpmem proxytest.mem
	python3 tools/check_proxytest.py proxytest.mem

test-dos: dostest.prg $(SDIMG)
	python3 tools/xrun.py 90 dostest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg dostest.prg -dumpmem dostest.mem
	python3 tools/check_dostest.py dostest.mem

all: hello.prg vtest.prg

hello.prg: hello.c
	$(CC) $(CFLAGS) -o $@ $<

vtest.prg: vtest.c m65_video.c m65_video_init.c m65_video.h
	$(CC) $(CFLAGS) -o $@ vtest.c m65_video.c m65_video_init.c

# Run the whole game headless for RUNSECS seconds; keeps the debug serial
# output (wolf.ser), a memory dump and a screenshot of where it got to.
RUNSECS ?= 60
run-wolf: wolf.prg $(SDIMG)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GB)/WOLF.OVL $(GB)/WOLF.DAT s:
	-python3 tools/xrun.py $(RUNSECS) wolf.ser WOLF-NEVER -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg wolf.prg -dumpmem wolf.mem -screenshot wolf.png
	@tr -d '\r' < wolf.ser | tail -20

# --- For the real machine --------------------------------------------------
# build/sdcard/: everything that goes in the root directory of the MEGA65's
# SD card: WOLF3D.D81 (a disk image holding the program, WOLF) and the files
# the game loads itself (see README.md).
SDCARD = build/sdcard
sdcard: wolf.prg $(PORTDATA)
	rm -rf $(SDCARD)
	mkdir -p $(SDCARD)
	cp $(GB)/WOLF.OVL $(GB)/WOLF.DAT $(PORTDATA) $(GAMEDATA) $(SDCARD)/
	c1541 -silent -format "wolfenstein 3d,w3" d81 $(SDCARD)/WOLF3D.D81 -write wolf.prg wolf
	@echo; echo "Copy everything in $(SDCARD)/ to the SD card's root directory."; ls -l $(SDCARD)

# build/wolf3d-mega65.zip: the same without the game data (*.WL1), for
# passing around: whoever installs it adds their own copy of the data.
dist: sdcard
	rm -f build/wolf3d-mega65.zip
	cd $(SDCARD) && zip -q ../wolf3d-mega65.zip WOLF3D.D81 WOLF.OVL WOLF.DAT SIGNON.BIN TABLES.BIN
	cp README.md build/README-mega65.md && cd build && zip -q wolf3d-mega65.zip README-mega65.md
	@ls -l build/wolf3d-mega65.zip

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
profile: wolf.prg $(SDIMG)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GB)/WOLF.OVL $(GB)/WOLF.DAT s:
	python3 tools/xrun.py 600 profile.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg wolf.prg -dumpmem profile.mem
	python3 tools/profile.py wolf.prg.elf profile.mem

# Play it in Xemu (a window), on the SD image that has the game's files:
# build/sd.img (a copy of your Xemu image; yours is left alone).
run-xemu: wolf.prg $(SDIMG)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GB)/WOLF.OVL $(GB)/WOLF.DAT s:
	$(XEMU) -sdimg $(SDIMG) -prg wolf.prg

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
           m65_sd.cpp host/host_sdl.cpp host/host_frame.cpp

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
check-frame: wolf.prg $(SDIMG) $(HB)/wolf-host
	$(MAKE) host-frames FRAMES=$(FRAME)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(GB)/WOLF.OVL $(GB)/WOLF.DAT s:
	python3 tools/xrun.py 300 frame.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg wolf.prg -dumpmem frame.mem
	python3 tools/check_frame.py frame.mem $(HB)/run/frame_$$(printf %04d $(FRAME)).raw \
	  $(WOLFDIR)/wolfpal.inc frame_$(FRAME).png

# Run a PRG headless for a while and save a screenshot: make shot PRG=vtest.prg
shot: $(PRG)
	timeout 60 $(XEMU) $(XEMUFLAGS) -prg $(PRG) -screenshot $(PRG:.prg=.png) -prgexit

clean:
	rm -f *.prg *.elf *.o *.png *.mem *.log *.ser; rm -rf obj build

.PHONY: all clean shot sid sid-midi fps profile sdcard dist run-wolf run-xemu host-frames check-frame wolf-objs size test-dos test-load test-ovl test-host test-huff test-ca test-title test-proxy
