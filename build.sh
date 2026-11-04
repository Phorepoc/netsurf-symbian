#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# build.sh -- the whole engine for one architecture (ARCH=x32 default, or arm): fetch the pinned sources, build curl, libpng,
# libjpeg, FreeType and QuickJS-ng, then NetSurf and its libraries, and stage the archives and resources the registry entries
# link and install. Inside the toolchain container (ghcr.io/phorepoc/base) with PHOREPOC_PDK / PHOREPOC_BUILD set (see
# README.md), after the tls repository's mbedTLS archive for the same ARCH (recipes/prebuild.sh does both). Each library
# is rebuilt from its pinned source every time; delete $PHOREPOC_BUILD/netsurf-symbian-work to start from scratch.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
export JOBS=${JOBS:-$(nproc)}
export ARCH=${ARCH:-${PHOREPOC_ARCH:-x32}}
r=$here/recipes
s() { echo "== $1 $(date +%T)"; }
if [ "$ARCH" = arm ] && ! command -v arm-none-eabi-gcc >/dev/null; then
  s "arm-none-eabi toolchain"; apt-get update -qq && apt-get install -y -qq --no-install-recommends gcc-arm-none-eabi binutils-arm-none-eabi
fi
s fetch;    bash $r/fetch_sources.sh
s deps;     bash $r/build_deps.sh
s netsurf;  bash $r/build_netsurf.sh
s done
