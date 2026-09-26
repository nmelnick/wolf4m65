/* Overlay prototype linker script.
 *
 *   $2001-$7FFF  resident code + data (loaded from the PRG)
 *   $8000-$BFFF  overlay window: every overlay is linked here (VMA)
 *   $C000-...    overlay images as stored in the PRG (LMA); the startup
 *                code copies them to attic RAM, then maps them at $8000.
 */

__basic_zp_start = 0x0002;
__basic_zp_end = 0x0090;

MEMORY {
    ram (rw) : ORIGIN = 0x2001, LENGTH = 0xafff   /* $2001-$CFFF, as the file */
    ovlwin (rx) : ORIGIN = 0x8000, LENGTH = 0x4000
}

INCLUDE commodore.ld

INPUT(unmap-basic.o)

SECTIONS {
    OVERLAY 0x8000 : AT (0xC000) {
        .ovl0 { *(.ovl0 .ovl0.*) }
        .ovl1 { *(.ovl1 .ovl1.*) }
    }

    __ovl0_lma = LOADADDR(.ovl0);  __ovl0_size = SIZEOF(.ovl0);
    __ovl1_lma = LOADADDR(.ovl1);  __ovl1_size = SIZEOF(.ovl1);
}

__stack = 0x8000;

OUTPUT_FORMAT {
    SHORT(0x2001)
    TRIM(ram)
}
