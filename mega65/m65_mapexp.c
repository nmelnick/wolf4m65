#include <stdint.h>

#include "m65_mapexp.h"

#define NEARTAG 0xa7
#define FARTAG  0xa8

// --- buffered sequential reader --------------------------------------------

#define INBUF 256

static uint8_t inbuf[INBUF];
static uint16_t inpos;
static farptr inaddr;           // far address of inbuf[0]

static void in_start(farptr p)
{
    inaddr = p;
    far_read(inbuf, inaddr, INBUF);
    inpos = 0;
}

static uint8_t in_byte(void)
{
    if (inpos == INBUF) {
        inaddr = FAR_ADD(inaddr, INBUF);
        far_read(inbuf, inaddr, INBUF);
        inpos = 0;
    }
    return inbuf[inpos++];
}

static uint16_t in_word(void)
{
    uint16_t lo = in_byte();
    return lo | (uint16_t)in_byte() << 8;
}

// --- buffered sequential writer (Carmack output) -----------------------------

#define OUTWORDS 128

static uint16_t outbuf[OUTWORDS];
static uint16_t outn;
static farptr outaddr;          // far address of outbuf[0]

static void out_flush(void)
{
    if (outn) {
        far_write(outaddr, outbuf, outn * 2);
        outaddr = FAR_ADD(outaddr, outn * 2);
        outn = 0;
    }
}

static void out_word(uint16_t w)
{
    outbuf[outn++] = w;
    if (outn == OUTWORDS)
        out_flush();
}

// Copy `count` words from far `src` to the output position, word by word
// forward, exactly like the original loop. The source may overlap the
// destination (a back-reference shorter than its length repeats a pattern),
// so copy in pieces no longer than the distance between them: each piece
// then only reads words that are already written.
static void out_copy(farptr src, uint16_t count)
{
    static uint16_t tmp[OUTWORDS];
    uint32_t dist;
    uint16_t n;

    out_flush();
    dist = (outaddr.a - src.a) / 2;
    while (count) {
        n = count;
        if (n > dist) n = (uint16_t)dist;
        if (n > OUTWORDS) n = OUTWORDS;
        far_read(tmp, src, n * 2);
        far_write(outaddr, tmp, n * 2);
        src = FAR_ADD(src, n * 2);
        outaddr = FAR_ADD(outaddr, n * 2);
        count -= n;
    }
}

void far_carmack_expand(farptr source, farptr dest, uint16_t length)
{
    uint16_t ch, chhigh, count, offset;
    int16_t left = (int16_t)(length / 2);

    in_start(source);
    outaddr = dest;
    outn = 0;

    while (left > 0)
    {
        ch = in_word();
        chhigh = ch >> 8;
        if (chhigh == NEARTAG || chhigh == FARTAG)
        {
            count = ch & 0xff;
            if (!count)
            {                               // a word containing the tag byte
                ch |= in_byte();
                out_word(ch);
                left--;
            }
            else
            {
                farptr copy;
                if (chhigh == NEARTAG)
                {
                    offset = in_byte();
                    // outptr - offset (outptr = flushed position + pending)
                    copy = FAR_ADD(outaddr, ((int32_t)outn - (int32_t)offset) * 2);
                }
                else
                {
                    offset = in_word();
                    copy = FAR_ADD(dest, (uint32_t)offset * 2);
                }
                left -= (int16_t)count;   // (uint16 arithmetic would wrap)
                if (left < 0)
                    break;
                out_copy(copy, count);
            }
        }
        else
        {
            out_word(ch);
            left--;
        }
    }
    out_flush();
}

void far_rlew_expand(farptr source, farptr dest, uint16_t length, uint16_t rlewtag)
{
    uint16_t value, count, i;
    uint16_t left = length / 2;             // words still to write

    in_start(source);
    outaddr = dest;
    outn = 0;
    do
    {
        value = in_word();
        if (value != rlewtag)
        {
            out_word(value);                // uncompressed
            left--;
        }
        else
        {
            count = in_word();              // compressed string
            value = in_word();
            for (i = 1; i <= count; i++)
                out_word(value);
            left -= count;
        }
    } while ((int16_t) left > 0);
    out_flush();
}
