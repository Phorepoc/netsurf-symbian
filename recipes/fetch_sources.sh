#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# fetch_sources.sh [name...] -- every source release of sources.lock (SHA-256 checked) into
# $PHOREPOC_BUILD/netsurf-symbian-work/dl/.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
while IFS=$'\t' read -r name file sha url; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ $# -gt 0 ] && ! printf '%s\n' "$@" | grep -qx "$name"; then continue; fi
    fetch_verified "$url" "$sha" "$DL/$file"
    echo "$name: $DL/$file"
done < "$NSS/sources.lock"
