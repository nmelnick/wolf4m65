// Overlay test, part 1. Everything in this file goes to an overlay.
#include "ovltest.h"

static int sq(int x) { return x * x; }          // static, only used via a pointer
int (*sq_ptr)(int) = sq;                         // data pointing at an overlay function

int add3(int a, int b, int c) { return a + b + c; }     // arguments in A/X and rc2..
long big(long a, long b) { return a * b + 1; }          // 32-bit in and out
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }   // recursion
int call_other(int x) { return ov2_f(x) + 1; }           // overlay -> other overlay
int tail(int x) { return add3(x, x, x); }                 // compiles to a tail jump
int deep(int n) { return n == 0 ? 0 : 1 + deep2(n - 1); } // ping-pong between overlays
