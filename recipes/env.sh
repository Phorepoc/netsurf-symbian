# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# Sourced by the recipes. Runs inside the toolchain container (ghcr.io/phorepoc/base):
#   PHOREPOC_PDK     the pdk checkout (tools/openc_cflags.py)
#   PHOREPOC_BUILD   all generated files (default: ../core/build)
#   PHOREPOC_ARCH or ARCH   x32 (default) or arm
# Prerequisite: the tls repository's mbedTLS archive for the same architecture (prebuild.sh builds it).
#
#   OUT   what the registry entries link and stage: $PHOREPOC_BUILD/thirdparty/{x86,arm}/netsurf/{lib,include,res}
#   SRC   the patched NetSurf tree of this architecture
NSS=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
: "${PHOREPOC_BUILD:=$NSS/../core/build}"
: "${PHOREPOC_PDK:=$NSS/../pdk}"
export PHOREPOC_BUILD PHOREPOC_PDK
ARCH=${ARCH:-${PHOREPOC_ARCH:-x32}}
if [ "$ARCH" = arm ]; then
    TP=arm; CC=arm-none-eabi-gcc; AR=arm-none-eabi-ar; OBJCOPY=arm-none-eabi-objcopy; OCARM=--arm
else
    TP=x86; CC=gcc; AR=ar; OBJCOPY=objcopy; OCARM=
fi
export CC AR OBJCOPY ARCH
JOBS=${JOBS:-$(nproc)}
NS_WORK=$PHOREPOC_BUILD/netsurf-symbian-work
DL=$NS_WORK/dl
V=$NS_WORK/$ARCH
SRC=$V/netsurf-all
OUT=$PHOREPOC_BUILD/thirdparty/$TP/netsurf
TLS_LIB=$PHOREPOC_BUILD/thirdparty/$TP/lib      # libmbedtls.a (the tls repository)
mkdir -p "$DL" "$V" "$OUT/lib" "$OUT/logs"

# fetch_verified URL SHA256 OUTFILE -- download unless OUTFILE already has that hash; fail on a mismatch
fetch_verified() {
    local url=$1 want=$2 out=$3 got
    if [ -f "$out" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" = "$want" ]; then return 0; fi
    mkdir -p "$(dirname "$out")"
    curl -fsSL --retry 3 -o "$out.part" "$url"
    got=$(sha256sum "$out.part" | cut -d' ' -f1)
    if [ "$got" != "$want" ]; then
        rm -f "$out.part"
        echo "sha256 mismatch for $url: got $got, want $want" >&2
        return 1
    fi
    mv "$out.part" "$out"
}
# tarball NAME -- the path of a pinned source file of sources.lock (fetch_sources.sh has downloaded it)
tarball() { awk -F'\t' -v n="$1" -v d="$DL" '$1 == n { print d "/" $2 }' "$NSS/sources.lock"; }
# unpack NAME DIR -- the pinned tarball of NAME into a fresh DIR (one top directory stripped)
unpack() { rm -rf "$2"; mkdir -p "$2"; tar -xf "$(tarball "$1")" -C "$2" --strip-components=1; }
# openc_flags -- the Open C client compile line (the pdk's tool) in XF, without -w, implicit declarations are errors
openc_flags() {
    local f flags
    mapfile -t flags < <(python3 "$PHOREPOC_PDK/tools/openc_cflags.py" --std=gnu11 $OCARM)
    [ "${#flags[@]}" -gt 10 ] || { echo "openc_cflags.py failed"; return 1; }
    XF=()
    for f in "${flags[@]}"; do [ "$f" = "-w" ] || XF+=("$f"); done
    XF+=(-Werror=implicit-function-declaration -Werror=implicit-int -Wno-attributes -fmax-errors=5)
}
# compile_all NAME SRCDIR -- compile the sources listed on stdin (relative to SRCDIR) with the flags in the file $OD/.flags,
# in parallel; archive them as $OUT/lib/NAME.a. Per-file logs in $OUT/logs/NAME/.
compile_one() {   # <srcdir> <source> <objdir> <logdir>
    local sd=$1 s=$2 od=$3 ld=$4 base
    base=$(echo "$s" | tr '/' '_'); base=${base%.*}
    mapfile -t F < "$od/.flags"
    if (cd "$sd" && "$CC" "${F[@]}" -c "$s" -o "$od/$base.o") > "$ld/$base.log" 2>&1; then
        echo "OK $s" >> "$od/.status"
    else
        echo "FAIL $s" >> "$od/.status"
    fi
}
export -f compile_one
# adaptation_dir NAME -- the checkout of a layer repository (PHOREPOC_ADAPTATIONS / PHOREPOC_ADAPTATION_NAMES, set by the pdk)
adaptation_dir() {
    local repos names i
    IFS=: read -r -a repos <<< "${PHOREPOC_ADAPTATIONS:-}"
    IFS=: read -r -a names <<< "${PHOREPOC_ADAPTATION_NAMES:-}"
    for i in "${!repos[@]}"; do
        if [ "${names[i]:-}" = "$1" ]; then echo "${repos[i]}"; return 0; fi
    done
    echo "$1 is not in PHOREPOC_ADAPTATIONS" >&2
    return 1
}
# write_flags NAME flag... -- the flags file of a library
write_flags() { local name=$1; shift; mkdir -p "$OUT/obj/$name"; printf '%s\n' "$@" > "$OUT/obj/$name/.flags"; }
# archive_lib NAME SRCDIR -- compile the sources on stdin (relative to SRCDIR) with the flags in $OUT/obj/NAME/.flags, in
# parallel, and archive them as $OUT/lib/NAME.a; all or nothing
archive_lib() {
    local name=$1 sd=$2 od=$OUT/obj/$1 ld=$OUT/logs/$1 n_ok n_fail f b
    mkdir -p "$ld"; rm -f "$od"/*.o "$ld"/*.log; : > "$od/.status"
    grep -v '^$' | xargs -P "$JOBS" -I{} bash -c 'compile_one "$@"' _ "$sd" {} "$od" "$ld"
    n_ok=$(grep -c '^OK' "$od/.status" || true); n_fail=$(grep -c '^FAIL' "$od/.status" || true)
    echo "$name: $n_ok ok, $n_fail failed"
    if [ "$n_fail" -gt 0 ]; then
        for f in $(grep '^FAIL' "$od/.status" | head -8 | awk '{print $2}'); do
            b=$(echo "$f" | tr '/' '_'); b=${b%.*}
            echo "--- $f"; grep -m3 error "$ld/$b.log" || true
        done
        return 1
    fi
    rm -f "$OUT/lib/$name.a"; "$AR" rcs "$OUT/lib/$name.a" "$od"/*.o
}
