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
WOLFSRCS = id_ca id_in id_pm id_us_1 id_vh id_vl wl_act1 wl_act2 wl_agent \
           wl_debug wl_draw wl_game wl_inter wl_main wl_menu wl_play \
           wl_state wl_text
WOLFOBJS = $(addprefix obj/,$(addsuffix .o,$(WOLFSRCS)))

# Shareware Wolf3D v1.4, all optional extras off.
WOLFFLAGS = -Os -fno-lto -I. -include m65_compat.h \
            -DVERSIONALREADYCHOSEN -DCARMACIZED -DUPLOAD -DMEGA65 \
            -Wno-c++11-narrowing -Wno-pragma-pack -Wno-switch

obj/%.o: $(WOLFDIR)/%.cpp m65_compat.h SDL.h
	@mkdir -p obj
	$(CXX) $(WOLFFLAGS) -c $< -o $@

wolf-objs: $(WOLFOBJS)

# Code/data size per section over all Wolf4SDL objects.
size: wolf-objs
	@for f in $(WOLFOBJS); do llvm-size -A $$f; done | \
	  awk '/^\./ {split($$1,a,"."); s[a[2]]+=$$2} END {for (k in s) print s[k], k}' | sort -rn

# --- Tests that need an SD card ---------------------------------------------
# Xemu boots the real Hyppo firmware from a copy of your Xemu SD image; the
# files in testsd/ are copied into the copy's root directory.

XEMUDIR ?= $(HOME)/.local/share/xemu-lgb/mega65
SDIMG    = build/sd.img

GAMEDATA = $(wildcard $(WOLFDIR)/*.WL1)

$(SDIMG): $(XEMUDIR)/mega65.img $(wildcard testsd/*) $(GAMEDATA)
	@mkdir -p build
	cp --sparse=always $< $@
	printf 'mtools_skip_check=1\ndrive s: file="$(abspath $@)" offset=1048576\n' > build/mtoolsrc
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o testsd/* $(GAMEDATA) s:

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

$(OT)/ovl.ld: $(OT)/ov1.s $(OT)/ov2.s tools/ovlgen.py
	python3 tools/ovlgen.py --out $(OT) --per-file $(OT)/ov1.s $(OT)/ov2.s

ovltest.prg: $(OT)/ovl.ld ovltest/main.c ovl/ovl_rt.s ovl/ovl_load.c m65_dos.c m65_video.c
	$(CC) -Os -fno-lto -Wall -T $(OT)/ovl.ld -Wl,--no-check-sections -o $@ ovltest/main.c \
	  $(OT)/ov1.ovl.s $(OT)/ov2.ovl.s $(OT)/ovl_thunks.s ovl/ovl_rt.s \
	  ovl/ovl_load.c m65_dos.c m65_video.c
	python3 tools/ovlpack.py $@.elf $$(grep -c '^overlay' $(OT)/plan.txt) $(OT)/OVLTEST.OVL

test-ovl: ovltest.prg $(SDIMG)
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o $(OT)/OVLTEST.OVL s:
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

test-host: build/hosttest/shifttest.sim
	mos-sim build/hosttest/shifttest.sim

# --- Far memory / Huffman test ------------------------------------------------

build/huffref.h: tools/gen_huffref.py $(GAMEDATA)
	@mkdir -p build
	python3 tools/gen_huffref.py $@

hufftest.prg: hufftest.c build/huffref.h m65_huff.c m65_huff.h m65_far.c m65_far.h m65_dos.c m65_video.c
	$(CC) $(CFLAGS) -o $@ hufftest.c m65_huff.c m65_far.c m65_dos.c m65_video.c

test-huff: hufftest.prg $(SDIMG)
	python3 tools/xrun.py 600 hufftest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg hufftest.prg -dumpmem hufftest.mem
	python3 tools/check_hufftest.py hufftest.mem build/huffref.h

test-dos: dostest.prg $(SDIMG)
	python3 tools/xrun.py 90 dostest.ser TEST-DONE -- $(XEMU) $(XEMUFLAGS) \
	  -sdimg $(SDIMG) -prg dostest.prg -dumpmem dostest.mem
	python3 tools/check_dostest.py dostest.mem

all: hello.prg vtest.prg

hello.prg: hello.c
	$(CC) $(CFLAGS) -o $@ $<

vtest.prg: vtest.c m65_video.c m65_video.h
	$(CC) $(CFLAGS) -o $@ vtest.c m65_video.c

# Run a PRG headless for a while and save a screenshot: make shot PRG=vtest.prg
shot: $(PRG)
	timeout 60 $(XEMU) $(XEMUFLAGS) -prg $(PRG) -screenshot $(PRG:.prg=.png) -prgexit

clean:
	rm -f *.prg *.elf *.o *.png *.mem *.log *.ser; rm -rf obj build

.PHONY: all clean shot wolf-objs size test-dos test-load test-ovl test-host test-huff
