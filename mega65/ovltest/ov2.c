// Overlay test, part 2: a different overlay from ov1.c.
#include "ovltest.h"

int ov2_f(int x) { return sq_ptr(x) + res_helper(x); }   // -> ov1 (sq), -> resident -> ov1
int deep2(int n) { return n == 0 ? 0 : 1 + deep(n - 1); }
