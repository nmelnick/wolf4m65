// Streaming Huffman expander for id's graphics files, far memory to far memory.
//
// Same algorithm as CAL_HuffExpand in id_ca.cpp, but source and destination
// are far addresses, moved through two small near buffers by DMA.

#ifndef M65_HUFF_H
#define M65_HUFF_H

#include <stdint.h>
#include "m65_far.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint16_t bit0, bit1; } m65_huffnode;  // layout of huffnode

void far_huff_expand(farptr src, farptr dst, uint32_t length,
                     const m65_huffnode *table);

#ifdef __cplusplus
}
#endif

#endif
