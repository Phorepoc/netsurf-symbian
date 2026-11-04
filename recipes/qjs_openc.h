/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* force-included into quickjs-ng's TUs: Open C gaps + failure logging. A failed assert / abort() names itself (file:line) in
 * stderr (= C:\netsurf.log on the Symbian port) before the process exits (qjs_compat.c). */
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
/* the libc gaps qjs_compat.c fills (Open C's libm / libpthread export neither) */
double log2(double x);
int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c);
void qjs_abort_at(const char *file, int line) __attribute__((noreturn));
#define abort() qjs_abort_at(__FILE__, __LINE__)
