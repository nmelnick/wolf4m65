#ifndef OVLTEST_H
#define OVLTEST_H

extern int (*sq_ptr)(int);
int add3(int a, int b, int c);
long big(long a, long b);
int fib(int n);
int call_other(int x);
int tail(int x);
int deep(int n);
int deep2(int n);
int ov2_f(int x);
int via_inner(int x);
int res_helper(int x);          // resident, in main.c

#endif
