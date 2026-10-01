// The game's AdLib sound effects as the original plays them, through the
// DOSBox OPL emulator (../dosbox/dbopl.cpp): the instrument as SDL_AlSetFXInst
// sets it (FM, no feedback), the notes stepped at 140Hz as the sound service
// does (a note keys on with the sound's block, a 0 keys off), then half a
// second for the release. One WAV per effect (OUTDIR/fxNN.wav, 44100Hz mono),
// for tools/adlib2sid.py to fit the SID versions to.
// usage: oplfx AUDIOHED AUDIOT OUTDIR [NSOUNDS]
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include "dbopl.h"

static std::vector<uint8_t> load(const char *p)
{
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    std::vector<uint8_t> d; int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t) c);
    fclose(f); return d;
}

static void wav(const std::string &path, const std::vector<int16_t> &s, int rate)
{
    FILE *f = fopen(path.c_str(), "wb");
    uint32_t n = s.size() * 2, v;
    fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); uint16_t h[] = {1, 1}; fwrite(h, 2, 2, f);
    v = rate; fwrite(&v, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f); uint16_t b[] = {2, 16}; fwrite(b, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(s.data(), 2, s.size(), f); fclose(f);
}

int main(int argc, char **argv)
{
    std::vector<uint8_t> hed = load(argv[1]), au = load(argv[2]);
    std::string out = argv[3];
    int nsounds = argc > 4 ? atoi(argv[4]) : 87;
    const int rate = 44100, per = rate / 140;            // samples per 140 Hz tick
    const uint32_t *off = (const uint32_t *) hed.data();
    int rendered = 0;
    for (int s = 0; s < nsounds; s++) {
        uint32_t a = off[nsounds + s], e = off[nsounds + s + 1];
        if (e <= a + 24) continue;
        uint32_t length; uint16_t prio; memcpy(&length, &au[a], 4); memcpy(&prio, &au[a + 4], 2);
        const uint8_t *in = &au[a + 6], block = au[a + 22], *data = &au[a + 23];
        if (!length || !(in[6] | in[7])) continue;
        DBOPL::Chip chip; chip.Setup(rate);
        for (int r = 0; r < 256; r++) chip.WriteReg(r, 0);
        chip.WriteReg(1, 0x20);                           // WSE
        const int m = 0, c = 3;                           // channel 0's cells
        chip.WriteReg(m + 0x20, in[0]); chip.WriteReg(m + 0x40, in[2]); chip.WriteReg(m + 0x60, in[4]);
        chip.WriteReg(m + 0x80, in[6]); chip.WriteReg(m + 0xE0, in[8]);
        chip.WriteReg(c + 0x20, in[1]); chip.WriteReg(c + 0x40, in[3]); chip.WriteReg(c + 0x60, in[5]);
        chip.WriteReg(c + 0x80, in[7]); chip.WriteReg(c + 0xE0, in[9]);
        chip.WriteReg(0xC0, 0);                           // FM, no feedback
        uint8_t keyblock = ((block & 7) << 2) | 0x20;
        std::vector<int16_t> pcm; std::vector<Bit32s> buf(per);
        auto run = [&](int ticks) {
            for (int t = 0; t < ticks; t++) {
                chip.GenerateBlock2(per, buf.data());
                for (int i = 0; i < per; i++) { int v = buf[i] * 2; pcm.push_back((int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v)); }
            }
        };
        for (uint32_t i = 0; i < length; i++) {
            if (data[i]) { chip.WriteReg(0xA0, data[i]); chip.WriteReg(0xB0, keyblock); }
            else chip.WriteReg(0xB0, 0);
            run(1);
        }
        chip.WriteReg(0xB0, 0);
        run(70);                                          // (0.5 s: the release)
        char name[32]; snprintf(name, sizeof name, "/fx%02d.wav", s);
        wav(out + name, pcm, rate);
        rendered++;
    }
    printf("%d effects rendered\n", rendered);
}
