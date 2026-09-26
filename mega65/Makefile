# Build for the MEGA65 using llvm-mos.
CC = mos-mega65-clang
CXX = mos-mega65-clang++
CFLAGS = -Os -Wall

XEMU = xemu-xmega65
XEMUFLAGS = -headless -sleepless -fastboot

all: hello.prg vtest.prg

hello.prg: hello.c
	$(CC) $(CFLAGS) -o $@ $<

vtest.prg: vtest.c m65_video.c m65_video.h
	$(CC) $(CFLAGS) -o $@ vtest.c m65_video.c

# Run a PRG headless for a while and save a screenshot: make shot PRG=vtest.prg
shot: $(PRG)
	timeout 60 $(XEMU) $(XEMUFLAGS) -prg $(PRG) -screenshot $(PRG:.prg=.png) -prgexit

clean:
	rm -f *.prg *.elf *.o *.png

.PHONY: all clean shot
