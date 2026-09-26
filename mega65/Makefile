# Build for the MEGA65 using llvm-mos.
CC = mos-mega65-clang
CXX = mos-mega65-clang++
CFLAGS = -Os

all: hello.prg

hello.prg: hello.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f *.prg *.elf

.PHONY: all clean
