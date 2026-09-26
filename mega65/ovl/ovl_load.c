#include <stdint.h>

#include "../m65_dos.h"
#include "../m65_far.h"
#include "../m65_video.h"
#include "ovl_load.h"

#define OVL_ATTIC 0x8100000UL        // MB $81; keep in step with ovl_rt.s
#define OVL_CHIP  0x20000UL          // chip RAM overlays; keep in step with ovlgen.py

extern const uint8_t __ovl_count;    // generated (ovl_thunks.s)
extern const uint16_t __ovl_slot;    // bytes per overlay (the window size)
extern const uint8_t __ovl_nchip;    // how many (the first ones) run from chip RAM
void __ovl_setmb(void);              // ovl_rt.s

// In .midtext (resident, in main memory, loaded with the data file): it runs
// only after m65_startup has loaded that, and prg has no room to spare.
__attribute__((section(".midtext.ovl_load")))
int ovl_load(const char *filename)
{
    int fd;
    unsigned k;

    __asm__ volatile("sei");

    if (m65_dos_init() != 0)
        return -1;
    fd = m65_dos_open(filename);
    if (fd < 0)
        return -2;

    for (k = 0; k < __ovl_count; k++) {
        if (m65_dos_read(fd, OVL_ATTIC + (uint32_t)k * __ovl_slot, __ovl_slot) != __ovl_slot) {
            m65_dos_close(fd);
            return -3;
        }
    }
    m65_dos_close(fd);

    // The hot overlays run from chip RAM ($20000-$3FFFF): code runs about 16
    // times slower from attic RAM on the real machine (hwtest.c). That area
    // holds the C65 ROM, which the game does not use, write-protected by the
    // hypervisor: Hyppo's rom_writeenable trap lifts the protection.
    if (__ovl_nchip) {
        __asm__ volatile("lda #$02\n sta $d641\n clv\n" ::: "a");
        far_poke(FAR(OVL_CHIP), 0x5A);
        if (far_peek(FAR(OVL_CHIP)) != 0x5A)
            return -4;                          // (still write-protected)
        for (k = 0; k < __ovl_nchip; k++)
            m65_dma_copy(OVL_CHIP + (uint32_t)k * __ovl_slot,
                         OVL_ATTIC + (uint32_t)k * __ovl_slot, __ovl_slot);
    }

    __ovl_setmb();
    return 0;
}
