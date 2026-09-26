#include <stdint.h>

#include "m65_huff.h"

#define BUF 256

static uint8_t inbuf[BUF], outbuf[BUF];

void far_huff_expand(farptr src, farptr dst, uint32_t length,
                     const m65_huffnode *table)
{
    const m65_huffnode *headptr = table + 254;     // head node is always 254
    const m65_huffnode *huffptr = headptr;
    uint16_t inpos = BUF, outpos = 0;
    uint16_t nodeval;
    uint8_t val = 0, mask = 0;

    while (length)
    {
        if (!mask)
        {
            if (inpos == BUF)
            {
                far_read(inbuf, src, BUF);
                src = FAR_ADD(src, BUF);
                inpos = 0;
            }
            val = inbuf[inpos++];
            mask = 1;
        }

        nodeval = (val & mask) ? huffptr->bit1 : huffptr->bit0;
        mask <<= 1;

        if (nodeval < 256)
        {
            outbuf[outpos++] = (uint8_t)nodeval;
            huffptr = headptr;
            length--;
            if (outpos == BUF || !length)
            {
                far_write(dst, outbuf, outpos);
                dst = FAR_ADD(dst, outpos);
                outpos = 0;
            }
        }
        else
            huffptr = table + (nodeval - 256);
    }
}
