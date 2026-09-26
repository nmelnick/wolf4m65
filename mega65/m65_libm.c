// atan and atan2 for the MEGA65 build (llvm-mos's libm lacks them), after
// fdlibm's s_atan.c and e_atan2.c. Verified against the host's libm by
// 'make test-libm' (tools/gen_libmtest.py).
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================

#include <stdint.h>
#include <string.h>

static int32_t hi(double x) { uint64_t u; memcpy(&u, &x, 8); return (int32_t)(u >> 32); }
static uint32_t lo(double x) { uint64_t u; memcpy(&u, &x, 8); return (uint32_t)u; }
static double fabs_(double x) { return x < 0 ? -x : x; }

static const double atanhi[] = {
    4.63647609000806093515e-01,     // atan(0.5)hi
    7.85398163397448278999e-01,     // atan(1.0)hi
    9.82793723247329054082e-01,     // atan(1.5)hi
    1.57079632679489655800e+00,     // atan(inf)hi
};

static const double atanlo[] = {
    2.26987774529616870924e-17,     // atan(0.5)lo
    3.06161699786838301793e-17,     // atan(1.0)lo
    1.39033110312309984516e-17,     // atan(1.5)lo
    6.12323399573676603587e-17,     // atan(inf)lo
};

static const double aT[] = {
     3.33333333333329318027e-01,
    -1.99999999998764832476e-01,
     1.42857142725034663711e-01,
    -1.11111104054623557880e-01,
     9.09088713343650656196e-02,
    -7.69187620504482999495e-02,
     6.66107313738753120669e-02,
    -5.83357013379057348645e-02,
     4.97687799461593236017e-02,
    -3.65315727442169155270e-02,
     1.62858201153657823623e-02,
};

double atan(double x)
{
    double w, s1, s2, z;
    int32_t ix, hx, id;

    hx = hi(x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x44100000) {                         // |x| >= 2^66
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lo(x) != 0))
            return x + x;                           // NaN
        return hx > 0 ? atanhi[3] + atanlo[3] : -atanhi[3] - atanlo[3];
    }
    if (ix < 0x3fdc0000) {                          // |x| < 0.4375
        if (ix < 0x3e200000)                        // |x| < 2^-29
            return x;
        id = -1;
    } else {
        x = fabs_(x);
        if (ix < 0x3ff30000) {                      // |x| < 1.1875
            if (ix < 0x3fe60000) {                  // 7/16 <= |x| < 11/16
                id = 0; x = (2.0 * x - 1.0) / (2.0 + x);
            } else {                                // 11/16 <= |x| < 19/16
                id = 1; x = (x - 1.0) / (x + 1.0);
            }
        } else {
            if (ix < 0x40038000) {                  // |x| < 2.4375
                id = 2; x = (x - 1.5) / (1.0 + 1.5 * x);
            } else {                                // 2.4375 <= |x| < 2^66
                id = 3; x = -1.0 / x;
            }
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0)
        return x - x * (s1 + s2);
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return hx < 0 ? -z : z;
}

static const double
    tiny   = 1.0e-300,
    pi_o_4 = 7.8539816339744827900E-01,
    pi_o_2 = 1.5707963267948965580E+00,
    pi     = 3.1415926535897931160E+00,
    pi_lo  = 1.2246467991473531772E-16;

double atan2(double y, double x)
{
    double z;
    int32_t k, m, hx, hy, ix, iy;
    uint32_t lx, ly;

    hx = hi(x); ix = hx & 0x7fffffff; lx = lo(x);
    hy = hi(y); iy = hy & 0x7fffffff; ly = lo(y);
    if (((uint32_t)ix | ((lx | -lx) >> 31)) > 0x7ff00000 ||
        ((uint32_t)iy | ((ly | -ly) >> 31)) > 0x7ff00000)
        return x + y;                               // NaN
    if (((hx - 0x3ff00000) | lx) == 0)
        return atan(y);                             // x = 1.0
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2);        // 2*sign(x) + sign(y)

    if ((iy | ly) == 0) {                           // y = 0
        switch (m) {
        case 0:
        case 1: return y;                           // atan(+-0, +anything) = +-0
        case 2: return pi + tiny;                   // atan(+0, -anything) = pi
        default: return -pi - tiny;                 // atan(-0, -anything) = -pi
        }
    }
    if ((ix | lx) == 0)                             // x = 0
        return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;

    if (ix == 0x7ff00000) {                         // x is INF
        if (iy == 0x7ff00000) {
            switch (m) {
            case 0: return pi_o_4 + tiny;
            case 1: return -pi_o_4 - tiny;
            case 2: return 3.0 * pi_o_4 + tiny;
            default: return -3.0 * pi_o_4 - tiny;
            }
        } else {
            switch (m) {
            case 0: return 0.0;
            case 1: return -0.0;
            case 2: return pi + tiny;
            default: return -pi - tiny;
            }
        }
    }
    if (iy == 0x7ff00000)                           // y is INF
        return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;

    k = (iy - ix) >> 20;
    if (k > 60)                                     // |y/x| > 2^60
        z = pi_o_2 + 0.5 * pi_lo;
    else if (hx < 0 && k < -60)                     // |y|/x < -2^60
        z = 0.0;
    else
        z = atan(fabs_(y / x));

    switch (m) {
    case 0: return z;                               // atan(+, +)
    case 1: return -z;                              // atan(-, +)
    case 2: return pi - (z - pi_lo);                // atan(+, -)
    default: return (z - pi_lo) - pi;               // atan(-, -)
    }
}
