// Map decompression in far memory: CAL_CarmackExpand and CA_RLEWexpand from
// id_ca.cpp, with source, intermediate buffer and final plane all in far
// memory.

#ifndef M65_MAPEXP_H
#define M65_MAPEXP_H

#include <stdint.h>
#include "m65_far.h"

#ifdef __cplusplus
extern "C" {
#endif

// length: EXPANDED length in bytes, as for CAL_CarmackExpand.
void far_carmack_expand(farptr source, farptr dest, uint16_t length);

// length: EXPANDED length in bytes, as for CA_RLEWexpand.
void far_rlew_expand(farptr source, farptr dest, uint16_t length, uint16_t rlewtag);

#ifdef __cplusplus
}
#endif

#endif
