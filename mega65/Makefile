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

all: hello.prg vtest.prg

hello.prg: hello.c
	$(CC) $(CFLAGS) -o $@ $<

vtest.prg: vtest.c m65_video.c m65_video.h
	$(CC) $(CFLAGS) -o $@ vtest.c m65_video.c

# Run a PRG headless for a while and save a screenshot: make shot PRG=vtest.prg
shot: $(PRG)
	timeout 60 $(XEMU) $(XEMUFLAGS) -prg $(PRG) -screenshot $(PRG:.prg=.png) -prgexit

clean:
	rm -f *.prg *.elf *.o *.png *.mem; rm -rf obj

.PHONY: all clean shot wolf-objs size
