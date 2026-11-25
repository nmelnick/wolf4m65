// Overlay loader: reads an .OVL file (see tools/ovlpack.py) into attic RAM and
// sets up the MAP hardware. Call once, before the first overlay function.

#ifndef OVL_LOAD_H
#define OVL_LOAD_H

// Returns 0 on success.
int ovl_load(const char *filename);

#endif
