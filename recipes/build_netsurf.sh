#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# build_netsurf.sh [lib ...] -- NetSurf and its libraries as static archives for Open C / P.I.P.S. (ARCH=x32 default, or arm).
# Run build_deps.sh first (curl, libpng, libjpeg, FreeType and QuickJS-ng are linked in / read from $OUT).
#
# Source: the pinned NetSurf tree of sources.lock (netsurf-all: NetSurf and its libraries; the fork with the QuickJS-ng
# JavaScript handler), unpacked into the work directory of this architecture and patched by patches/*.patch (the Symbian seams:
# platform guards beside the BSD/Windows ones, the resource path of the engine, a default stack for QuickJS).
#
# Per library:
#   1. NetSurf's OWN makefiles (buildsystem/, run natively) produce the generated sources (parserutils aliases.inc, hubbub
#      entities.inc, libcss's gen_parser) and tell us SOURCES + CFLAGS (a `print-%` rule appended with a second -f). No
#      hand-kept file lists.
#   2. Each source is compiled with the Open C client line (the pdk's openc_cflags.py) plus the library's own -D/-I.
#      Implicit declarations are ERRORS: a libc function Open C lacks shows up here, not as an unresolved import at link time.
#   3. ar -> $OUT/lib/<lib>.a; per-file logs in $OUT/logs/<lib>/.
# `netsurf` (last) is the browser core with the framebuffer front end (the engine drives it through libnsfb's "ram" surface).
#
# Out: $OUT/lib/*.a, $OUT/include/libnsfb (the staged headers), $OUT/res (the run-time resources the package installs).
# Env: JOBS (nproc), APT=1 (default: apt-get gperf, flex, bison, pkg-config into this throwaway container; APT=0 = off).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
HERE=$NSS
WORK=$V/work
NS_LIBSET="libwapcaplet libparserutils libhubbub libcss libdom libnsutils libnsbmp libnsgif libutf8proc libnsfb libnslog libnspsl libsvgtiny"
LIBS=${*:-$NS_LIBSET netsurf}

bash "$NSS/recipes/fetch_sources.sh" netsurf >/dev/null || exit 1
for f in "$OUT/lib/libcurl.a" "$OUT/lib/libpng.a" "$OUT/lib/libjpeg.a" "$OUT/lib/libfreetype.a" "$OUT/lib/libquickjs.a"; do
    [ -f "$f" ] || { echo "no $f -- run recipes/build_deps.sh first"; exit 1; }
done

# code generators the image may lack: gperf (hubbub's element-type table), flex + bison (libnslog's filter parser), pkg-config
if [ "${APT:-1}" = 1 ] && ! { command -v gperf && command -v bison && command -v flex && command -v pkg-config; } >/dev/null; then
    for try in 1 2 3; do
        (timeout 300 apt-get update -qq && timeout 300 apt-get install -y -qq gperf flex bison pkg-config) >/dev/null 2>&1 && break
        echo "apt: attempt $try failed"; sleep 10
    done
    command -v pkg-config >/dev/null || { echo "apt: gperf/flex/bison/pkg-config not installed -- stop"; exit 1; }
fi

if [ ! -f "$SRC/.patched" ]; then
    unpack netsurf "$SRC" || exit 1
    for p in "$NSS"/patches/*.patch; do
        patch -s -p1 -d "$SRC" < "$p" || { echo "patch FAILED: $(basename "$p")"; exit 1; }
    done
    touch "$SRC/.patched"
fi
mkdir -p "$WORK"
NSSHARED=$SRC/buildsystem
# $(info), not echo: CFLAGS carry quotes/parens a shell would choke on
printf 'print-%%:\n\t@:$(info @@$*=$($*))\n' > $WORK/print.mk

openc_flags || exit 1
# libdom's `make install` puts its parser bindings under include/dom/bindings/; stage the same view
STAGE=$WORK/stage/include
mkdir -p "$STAGE/dom/bindings"
ln -sfn "$SRC/libdom/bindings/hubbub" "$STAGE/dom/bindings/hubbub"
ln -sfn "$SRC/libdom/bindings/xml" "$STAGE/dom/bindings/xml"
ALLINC=("-I$STAGE")
for l in $NS_LIBSET; do ALLINC+=("-I$SRC/$l/include"); done
ALLINC+=("-I$OUT/img-include")

# .pc files for the libraries, so the browser's own Makefile (pkg-config feature detection) sees them. Cflags only: the
# archives are linked by the component build, not by NetSurf's makefiles.
PCDIR=$WORK/pc
mkdir -p "$PCDIR"
for l in $NS_LIBSET; do
    printf 'Name: %s\nDescription: %s\nVersion: 99\nCflags: -I%s -I%s\nLibs:\n' \
        "$l" "$l" "$SRC/$l/include" "$STAGE" > "$PCDIR/$l.pc"
done
printf 'Name: freetype2\nDescription: FreeType 2.13.3\nVersion: 25.1.19\nCflags: -I%s\nLibs:\n' "$OUT/ft-include" > "$PCDIR/freetype2.pc"
printf 'Name: libpng\nDescription: libpng\nVersion: 1.6.44\nCflags: -I%s\nLibs:\n' "$OUT/img-include" > "$PCDIR/libpng.pc"
printf 'Name: libcurl\nDescription: libcurl\nVersion: 8.10.1\nCflags: -I%s -include sys/select.h\nLibs:\n' "$OUT/curl-include" > "$PCDIR/libcurl.pc"
export PKG_CONFIG_PATH=$PCDIR PKG_CONFIG_LIBDIR=$PCDIR

# The browser: core + framebuffer front end; curl (+ mbedTLS under it), PNG/JPEG, FreeType fonts, the QuickJS-ng handler.
# Off: OpenSSL (curl uses mbedTLS), Duktape, WebP/JPEG XL, RSVG, RISC OS sprites, video, PDF export, the sanitizer.
NS_BROWSER_ARGS=(TARGET=framebuffer NETSURF_USE_CURL=YES NETSURF_USE_OPENSSL=NO NETSURF_USE_JPEG=YES NETSURF_USE_PNG=YES
    NETSURF_USE_DUKTAPE=NO NETSURF_USE_WEBP=NO NETSURF_USE_JPEGXL=NO NETSURF_USE_RSVG=NO NETSURF_USE_ROSPRITE=NO
    NETSURF_USE_VIDEO=NO NETSURF_USE_HARU_PDF=NO NETSURF_USE_LIBICONV_PLUG=NO NETSURF_USE_NSSVG=NO
    NETSURF_FB_FONTLIB=freetype NETSURF_USE_SANITIZER=NO NETSURF_FRAMEBUFFER_RESOURCES=/resource/phenetsurf/
    NETSURF_USE_QUICKJS=YES)

mkdir -p "$OUT/obj"
SUMMARY=$OUT/summary.txt
: > "$SUMMARY"

for lib in $LIBS; do
    d=$SRC/$lib
    [ -d "$d" ] || { echo "no such lib $lib" | tee -a "$SUMMARY"; continue; }
    od=$OUT/obj/$lib; ld=$OUT/logs/$lib
    rm -rf "$od" "$ld"; mkdir -p "$od" "$ld"
    mk=()
    [ "$lib" = netsurf ] && mk=("${NS_BROWSER_ARGS[@]}")
    info=$(make -s -C "$d" -f Makefile -f $WORK/print.mk NSSHARED="$NSSHARED" "${mk[@]}" \
                print-SOURCES print-CFLAGS print-IFLAGS print-PRE_TARGETS print-OBJROOT print-MESSAGES 2> "$ld/_generate.log")
    pre=$(echo "$info" | sed -n 's/^@@PRE_TARGETS=//p')
    srcs=$(echo "$info" | sed -n 's/^@@SOURCES=//p')
    objroot=$(echo "$info" | sed -n 's/^@@OBJROOT=//p')
    # generated SOURCES that do not exist yet (the browser's image-*.c from PNGs via convert_image, font-ns-sans.c via
    # convert_font, both host tools; the embedded QuickJS runtime) + its testament.h
    if [ "$lib" = netsurf ]; then
        for s in $srcs; do case $s in build/*) [ -f "$d/$s" ] || pre="$pre $s" ;; esac; done
        pre="$pre $objroot/quickjs/runtime.js.inc"
        pre="$pre $objroot/testament.h $(echo "$info" | sed -n 's/^@@MESSAGES=//p')"
    fi
    # PRE_TARGETS (hubbub's gperf table) and the above are built by the lib's own rules, natively
    [ -n "${pre// /}" ] && make -s -C "$d" NSSHARED="$NSSHARED" "${mk[@]}" $pre >> "$ld/_generate.log" 2>&1
    # IFLAGS: the browser's own -I . include $(OBJROOT) (the libs put theirs in CFLAGS)
    libcf="$(echo "$info" | sed -n 's/^@@IFLAGS=//p') $(echo "$info" | sed -n 's/^@@CFLAGS=//p')"
    case $lib in
        # the expat XML binding: NetSurf's HTML path is the hubbub one
        libdom) srcs=$(echo "$srcs" | tr ' ' '\n' | grep -v '^bindings/xml/' | tr '\n' ' ') ;;
    esac
    # the library's own -D / -I (its warnings, -O, -std, -pedantic, -Werror are replaced by ours), FIRST: the Open C include
    # tree is one flat namespace of generic names (palette.h, surface.h...) that would shadow a library's own quoted headers.
    # eval = shell quote removal (-D_ALIGNED="__attribute__((aligned))").
    : > "$od/.flags"
    eval "set -- $libcf"
    for f in "$@"; do
        case $f in -D*|-I*) echo "$f" >> "$od/.flags" ;; esac
    done
    for f in "${XF[@]}"; do echo "$f" >> "$od/.flags"; done
    for f in "${ALLINC[@]}"; do echo "$f" >> "$od/.flags"; done
    if [ "$lib" = netsurf ]; then
        printf '%s\n' -include "$NSS/os/src/netsurf/nsport_symbian.h" -include sys/select.h "-I$OUT/qjs-include" >> "$od/.flags"
        # the curl fetcher parses the CA bundle once into a shared mbedTLS chain (patch 0105): mbedTLS's headers with the
        # configuration curl was built with
        printf '%s\n' -DNS_SHARED_MBEDTLS_CA '-DMBEDTLS_CONFIG_FILE="phorepoc_mbedtls_config.h"' \
            "-I$(adaptation_dir tls)/os/src/mbedtls" "-I$PHOREPOC_BUILD/tls/mbedtls-src/include" >> "$od/.flags"
    fi
    # parserutils' own codecs (UTF-8/16, ISO-8859-x, Windows-125x, ASCII) instead of iconv: Open C's iconv_open("UTF-8",
    # "Windows-1252") fails -> every page without a UTF-8 charset = BadEncoding
    [ "$lib" = libparserutils ] && echo -DWITHOUT_ICONV_FILTER >> "$od/.flags"
    : > "$od/.status"
    echo "$srcs" | tr ' ' '\n' | grep -v '^$' |
        xargs -P "$JOBS" -I{} bash -c 'compile_one "$@"' _ "$d" {} "$od" "$ld"
    n_ok=$(grep -c '^OK' "$od/.status"); n_fail=$(grep -c '^FAIL' "$od/.status")
    rm -f "$OUT/lib/$lib.a"
    # the engine (os/src/phewebengine_netsurf/phengine_ns.c) drives the core itself: the front end's main() becomes
    # netsurf_main so the plug-in does not export a main
    if [ "$lib" = netsurf ] && [ -f "$od/frontends_framebuffer_gui.o" ]; then
        "$OBJCOPY" --redefine-sym main=netsurf_main "$od/frontends_framebuffer_gui.o"
    fi
    # the engine references the ram surface's registration function to pull its archive member: make it global
    [ "$lib" = libnsfb ] && [ -f "$od/src_surface_ram.o" ] && "$OBJCOPY" --globalize-symbol=ram_register_surface "$od/src_surface_ram.o"
    # a partial archive would only muddy the link: all or nothing
    if [ "$n_ok" -gt 0 ] && [ "$n_fail" -eq 0 ]; then "$AR" rcs "$OUT/lib/$lib.a" "$od"/*.o; fi
    line=$(printf '%-16s %3d/%-3d compiled' "$lib" "$n_ok" "$((n_ok + n_fail))")
    [ "$n_fail" -eq 0 ] && [ "$n_ok" -gt 0 ] && line="$line  -> lib/$lib.a" || line="$line  FAIL: $(grep '^FAIL' "$od/.status" | cut -d' ' -f2 | head -6 | tr '\n' ' ')"
    echo "$line" | tee -a "$SUMMARY"
    grep '^FAIL' "$od/.status" | cut -d' ' -f2 | head -12 | while read -r s; do
        b=$(echo "$s" | tr '/' '_'); b=${b%.c}
        e=$(grep -m1 -E 'error|Error' "$ld/$b.log" | cut -c1-240)
        echo "    $s: $e" | tee -a "$SUMMARY"
    done
done
if grep -q 'FAIL' "$SUMMARY"; then echo "build_netsurf: failures, see $SUMMARY"; exit 1; fi

# --- staging for the component build (the registry entries read these) ---------------------------------------------
# libnsfb's public and internal headers (the engine uses the surface API)
rm -rf "$OUT/include"; mkdir -p "$OUT/include/libnsfb"
cp "$SRC"/libnsfb/include/*.h "$SRC"/libnsfb/src/*.h "$OUT/include/libnsfb/"
# run-time resources -> \resource\phenetsurf\ of the install drive
FBRES=$SRC/netsurf/frontends/framebuffer/res
rm -rf "$OUT/res"; mkdir -p "$OUT/res"
for f in default.css internal.css quirks.css adblock.css welcome.html credits.html licence.html; do
    cp -L "$FBRES/$f" "$OUT/res/$f" 2>/dev/null || cp -L "$FBRES/en/$f" "$OUT/res/$f" 2>/dev/null || echo "res: no $f"
done
cp -L "$FBRES/en/Messages" "$OUT/res/Messages" || echo "res: no Messages"
# https: the CA bundle of the tls repository (recipes/fetch_cacert.sh) and the Choices file naming it and the faces
cp "$PHOREPOC_BUILD/thirdparty/x86/res/ca-bundle.crt" "$OUT/res/ca-bundle.crt" || { echo "res: no CA bundle (the tls repository's prepare recipe)"; exit 1; }
cp "$NSS/os/data/netsurf/Choices.$ARCH" "$OUT/res/Choices"
ls "$OUT/res" | wc -l
