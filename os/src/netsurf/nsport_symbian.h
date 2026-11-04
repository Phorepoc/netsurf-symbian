/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* nsport_symbian.h -- force-included into every NetSurf (browser) translation unit (see recipes/build_netsurf.sh).
 *
 * Open C / P.I.P.S. header gaps that are cheaper to close here than by patching many files:
 *  - PATH_MAX: stdapis/limits.h deliberately skips <sys/syslimits.h> when __SYMBIAN32__ is set, so
 *    PATH_MAX (256 on Symbian -- the file server's KMaxFileName) only comes from sys/syslimits.h or
 *    sys/param.h. NetSurf expects it from <limits.h> (utils/filepath.c, frontends/framebuffer/fetch.c).
 * Everything else Symbian-specific is a patch (../patches/) guarded by __SYMBIAN32__.
 */
#ifndef NSPORT_SYMBIAN_H
#define NSPORT_SYMBIAN_H
#include <limits.h>
#ifndef PATH_MAX
#include <sys/syslimits.h>
#endif
#endif
