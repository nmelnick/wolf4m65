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

$(SDIMG): $(XEMUDIR)/mega65.img $(wildcard testsd/*)
	@mkdir -p build
	cp --sparse=always $< $@
	printf 'mtools_skip_check=1\ndrive s: file="$(abspath $@)" offset=1048576\n' > build/mtoolsrc
	MTOOLSRC=$(abspath build/mtoolsrc) mcopy -o testsd/* s:

dostest.prg: dostest.c m65_dos.c m65_dos.h m65_video.c m65_video.h
	$(CC) $(CFLAGS) -o $@ dostest.c m65_dos.c m65_video.c

test-dos: dostest.prg $(SDIMG)
	timeout 90 $(XEMU) $(XEMUFLAGS) -sdimg $(SDIMG) -prg dostest.prg \
	  -dumpmem dostest.mem > dostest.log 2>&1 || true
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
	rm -f *.prg *.elf *.o *.png *.mem *.log; rm -rf obj build

.PHONY: all clean shot wolf-objs size test-dos
