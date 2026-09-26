#include <stdint.h>

#include "../m65_dos.h"
#include "ovl_load.h"

#define OVL_ATTIC 0x8100000UL        // MB $81; keep in step with ovl_rt.s
#define OVL_SLOT  0x4000UL

extern const uint8_t __ovl_count;    // generated (ovl_thunks.s)
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
        if (m65_dos_read(fd, OVL_ATTIC + k * OVL_SLOT, OVL_SLOT) != OVL_SLOT) {
            m65_dos_close(fd);
            return -3;
        }
    }
    m65_dos_close(fd);

    __ovl_setmb();
    return 0;
}
