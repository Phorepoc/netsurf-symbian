#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only
#
# sis.sh -- the pdk's package.yml runs it after the components: the engine plug-in as
# $PHOREPOC_BUILD/sis/netsurf-symbian-<arch>.sis (the DLL, its ECom registration, the resources and the CA bundle).
set -eu
ARCH=${PHOREPOC_ARCH:-x32}
if [ "$ARCH" = arm ]; then KEY=phewebengine_netsurf_arm; AFL=--arm; else KEY=phewebengine_netsurf; AFL=; fi
# The SIS carries its release's version (PHOREPOC_PACKAGE_VERSION, set by the pdk's package.yml), so a newer release is an
# upgrade. It installs through the Catalogue; no image carries an engine.
# The licences of everything in the DLL (GPL-2.0 for NetSurf, the libraries' own, MPL-2.0 for the CA bundle), NOTICE and the
# written source offer go with it, to \resource\phweb\netsurf\licences\.
REPO=$(cd "$(dirname "$0")/.." && pwd)
LIC=
for f in "$REPO"/LICENSES/* "$REPO/LICENSE" "$REPO/NOTICE" "$REPO/SOURCE-OFFER.txt"; do
  LIC="$LIC --file $f=!:/resource/phweb/netsurf/licences/$(basename "$f")"
done
python3 "$PHOREPOC_PDK/tools/make_sis.py" $KEY $AFL --unsigned --version "${PHOREPOC_PACKAGE_VERSION:-1.0.0}" \
  --name "NetSurf web engine" --vendor Phorepoc $LIC \
  --out "$PHOREPOC_BUILD/sis/netsurf-symbian-$ARCH.sis"
