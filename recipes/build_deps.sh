#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# build_deps.sh [lib ...] -- the C libraries the engine links besides NetSurf's own, each as a static archive for Open C /
# P.I.P.S. (ARCH=x32 default, or arm), compiled with the pdk's Open C client line (tools/openc_cflags.py):
#   libcurl 8.10.1 (over the tls repository's mbedTLS), libpng 1.6.44, IJG libjpeg 9f, FreeType 2.13.3, quickjs-ng 0.17.0
# Out: $OUT/lib/*.a and the headers the NetSurf build uses: $OUT/{curl,img,ft,qjs}-include.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
LIBS=${*:-libcurl libpng libjpeg libfreetype libquickjs}
bash "$NSS/recipes/fetch_sources.sh" curl libpng libjpeg freetype quickjs >/dev/null
openc_flags
W=$V/deps
mkdir -p "$W"

for lib in $LIBS; do
  od=$OUT/obj/$lib; mkdir -p "$od"
  case $lib in
  libcurl)
    S=$W/curl; unpack curl "$S"
    # curl's configure cannot run for this target: our curl_config.h (what it would have found) and the plain-C sources of
    # lib/Makefile.inc. curl.h needs Open C's <sys/select.h> for fd_set (forced in here and in NetSurf's libcurl.pc).
    cp -f "$NSS/recipes/curl_config.h" "$S/lib/curl_config.h"
    # curl 8.10.1's mbedTLS backend takes mbedtls_ssl_read() == 0 (the server closed TCP without a close_notify) for an
    # error: a body that ends with the connection (HTTP/1.1 with no Content-Length, e.g. wpt.live's Python server) failed
    # with CURLE_RECV_ERROR (56, "ssl_read returned: (-0x0000)") after all of it had arrived. It is the end of the body.
    sed -i 's/^    if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)$/    if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || !ret)   \/* Phorepoc: EOF *\//' \
        "$S/lib/vtls/mbedtls.c"
    grep -q 'Phorepoc: EOF' "$S/lib/vtls/mbedtls.c" || { echo "curl mbedtls.c EOF edit missed"; exit 1; }
    TLSDIR=$(adaptation_dir tls); MBED=$PHOREPOC_BUILD/tls/mbedtls-src
    [ -f "$TLS_LIB/libmbedtls.a" ] || { echo "no $TLS_LIB/libmbedtls.a (prebuild.sh builds it from the tls repository)"; exit 1; }
    # (-iquote lib/: the Open C line has stdapis/openssl on the path, whose rand.h shadowed curl's own "rand.h")
    write_flags libcurl "${XF[@]}" -include sys/select.h -DHAVE_CONFIG_H -iquote "$S/lib" -I"$NSS/recipes" -I"$S/include" -I"$S/lib" \
        -DUSE_MBEDTLS=1 '-DMBEDTLS_CONFIG_FILE="phorepoc_mbedtls_config.h"' -I"$TLSDIR/os/src/mbedtls" -I"$MBED/include"
    srcs=$(cd "$S/lib" && printf 'include Makefile.inc\nprint:\n\t@echo $(CSOURCES)\n' | make -s -f - print)
    [ -n "$srcs" ] || { echo "no CSOURCES from curl's lib/Makefile.inc"; exit 1; }
    printf '%s\n' $srcs | archive_lib libcurl "$S/lib"
    rm -rf "$OUT/curl-include"; mkdir -p "$OUT/curl-include"; cp -r "$S/include/curl" "$OUT/curl-include/"
    ;;
  libpng)
    S=$W/libpng; unpack libpng "$S"; cp "$S/scripts/pnglibconf.h.prebuilt" "$S/pnglibconf.h"
    # plain C filters (no SIMD). -U__DLL__: Open C's __DLL__ is also Borland's "building a DLL" and libpng then wants
    # PNG_DLL_EXPORT; this is a static archive
    write_flags libpng "${XF[@]}" -iquote "$S" -U__DLL__ -DPNG_INTEL_SSE_OPT=0 -DPNG_ARM_NEON_OPT=0 -DPNG_MIPS_MSA_OPT=0 \
        -DPNG_POWERPC_VSX_OPT=0 -DPNG_LOONGARCH_LSX_OPT=0
    printf '%s.c\n' png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite \
        pngwtran pngwutil | archive_lib libpng "$S"
    mkdir -p "$OUT/img-include"; cp "$S/png.h" "$S/pngconf.h" "$S/pnglibconf.h" "$OUT/img-include/"
    ;;
  libjpeg)
    S=$W/jpeg; unpack libjpeg "$S"; cp "$NSS/recipes/jconfig.h" "$S/jconfig.h"
    write_flags libjpeg "${XF[@]}" -iquote "$S"
    # makefile.ansi's LIBOBJECTS, with the no-backing-store memory manager (jmemnobs)
    printf '%s.c\n' jcapimin jcapistd jcarith jctrans jcparam jdatadst jcinit jcmaster jcmarker jcmainct jcprepct \
        jccoefct jccolor jcsample jchuff jcdctmgr jfdctfst jfdctflt jfdctint jdapimin jdapistd jdarith jdtrans jdatasrc \
        jdmaster jdinput jdmarker jdhuff jdmainct jdcoefct jdpostct jddctmgr jidctfst jidctflt jidctint jdsample \
        jdcolor jquant1 jquant2 jdmerge jaricom jcomapi jutils jerror jmemmgr jmemnobs | archive_lib libjpeg "$S"
    mkdir -p "$OUT/img-include"; cp "$S/jpeglib.h" "$S/jconfig.h" "$S/jmorecfg.h" "$S/jerror.h" "$OUT/img-include/"
    ;;
  libfreetype)
    S=$W/freetype; unpack freetype "$S"
    # the unified base, the optional base components the modules and NetSurf use, the modules of recipes/ftmodules.h, the cache,
    # and the gzip support sfnt's WOFF fonts decompress with (over the system zlib, libz.dll)
    write_flags libfreetype "${XF[@]}" -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<ftmodules.h>' -DFT_CONFIG_OPTION_SYSTEM_ZLIB \
        -I"$NSS/recipes" -I"$S/include" -iquote "$S/src"
    printf 'src/%s.c\n' base/ftsystem base/ftinit base/ftbase base/ftdebug base/ftglyph base/ftbitmap base/ftbbox base/ftmm \
        base/ftsynth base/ftstroke autofit/autofit cff/cff psaux/psaux psnames/psnames pshinter/pshinter raster/raster \
        sfnt/sfnt smooth/smooth truetype/truetype cache/ftcache gzip/ftgzip | archive_lib libfreetype "$S"
    rm -rf "$OUT/ft-include"; mkdir -p "$OUT/ft-include"; cp -r "$S/include/." "$OUT/ft-include/"
    ;;
  libquickjs)
    S=$W/quickjs; unpack quickjs "$S"
    # Open C: no malloc.h / malloc_usable_size (0 = "unknown" to the engine); clock_gettime(CLOCK_MONOTONIC) fails there and
    # quickjs would abort(): take the gettimeofday branch
    sed -i "/static inline uint64_t js__hrtime_ns(void) {/{n;s/#ifdef __DJGPP/#if 1/}" "$S/cutils.h"
    sed -i -e "s|^#include <malloc.h>|/* no malloc.h */|" -e "s|return malloc_usable_size(.*|return 0;|" "$S/cutils.h"
    cp "$NSS/recipes/qjs_compat.c" "$S/qjs_compat.c"
    flags=("${XF[@]}" -O2 -Wno-implicit-fallthrough -DGCC_BUILTIN_ATOMICS -Dalloca=__builtin_alloca "-DCONFIG_VERSION=\"0.17.0\""
           -include "$NSS/recipes/qjs_openc.h" -iquote "$S")
    # x32: GCC's switch lookup tables link out of range; ARM (ARMv6): no 64-bit __atomic ops, so no Atomics object
    if [ "$ARCH" = arm ]; then flags+=(-D__STDC_NO_ATOMICS__=1); else flags+=(-fno-tree-switch-conversion); fi
    write_flags libquickjs "${flags[@]}"
    printf '%s.c\n' quickjs libregexp libunicode dtoa qjs_compat | archive_lib libquickjs "$S"
    rm -rf "$OUT/qjs-include"; mkdir -p "$OUT/qjs-include/quickjs"; cp "$S/quickjs.h" "$OUT/qjs-include/quickjs/"
    ;;
  *) echo "unknown library $lib"; exit 1 ;;
  esac
done
