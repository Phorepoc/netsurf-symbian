/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* libc gaps of Open C that quickjs-ng uses (the export tables of libc/libm/libpthread lack them) */
#include <math.h>
#include <pthread.h>
#include <time.h>

double log2(double x)
{
    int e;
    double m = frexp(x, &e);          /* x = m * 2^e, 0.5 <= m < 1 */
    if (m == 0.5)
        return (double)(e - 1);       /* exact for powers of two (Math.log2(8) === 3) */
    if (!(m > 0))
        return log(x);                /* 0, negative, NaN, inf: log()'s answers */
    return log(m) * 1.4426950408889634074 + (double)e;
}

/* the engine asks for a monotonic condvar clock; Open C's condvars have one clock */
int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c) { (void)a; (void)c; return 0; }

/* ---- failure logging ------------------------------------------------------------------------------------------------ */
#include <stdio.h>
/* Open C's assert() calls __assert(func, file, line, expr) (assert.h): say what failed, then leave with a distinctive code */
void __assert(const char *func, const char *file, int line, const char *expr)
{
    fprintf(stderr, "QJS-ASSERT %s:%d in %s: %s\n", file, line, func ? func : "?", expr ? expr : "?");
    fflush(stderr);
    exit(97);
}

void qjs_abort_at(const char *file, int line)
{
    fprintf(stderr, "QJS-ABORT %s:%d\n", file, line);
    fflush(stderr);
    exit(98);
}
