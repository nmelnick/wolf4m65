#include <stdint.h>

#include "../m65_dos.h"
#include "ovl_load.h"

#define OVL_ATTIC 0x8100000UL        // MB $81; keep in step with ovl_rt.s

extern const uint8_t __ovl_count;    // generated (ovl_thunks.s)
extern const uint16_t __ovl_slot;    // bytes per overlay (the window size)
void __ovl_setmb(void);              // ovl_rt.s

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

    __ovl_setmb();
    return 0;
}
