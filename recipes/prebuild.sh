#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# prebuild.sh -- the pdk's package.yml runs it in the toolchain container before the components: the tls repository's mbedTLS
# archive and CA bundle for PHOREPOC_ARCH, then the whole engine (build.sh).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
export ARCH=${PHOREPOC_ARCH:-x32} JOBS=${JOBS:-$(nproc)}
. "$HERE/recipes/env.sh"
tls=$(adaptation_dir tls)
bash "$tls/recipes/fetch_mbedtls.sh"
bash "$tls/recipes/fetch_cacert.sh"
if [ "$ARCH" = arm ]; then bash "$tls/recipes/build_mbedtls.sh" --arm; else bash "$tls/recipes/build_mbedtls.sh"; fi
exec bash "$HERE/build.sh"
