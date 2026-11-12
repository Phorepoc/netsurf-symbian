# Copyright The Phorepoc Authors
# SPDX-License-Identifier: GPL-2.0-only

r"""components_netsurf -- the netsurf-symbian layer's components (merged into core's registry when this repository is built:
PHOREPOC_ADAPTATIONS=<board adaptations>:<tls>:<webengine>:<netsurf-symbian>, see the pdk's tools/emitter/paths.py).

OS paths are relative to the merged OS tree. NetSurf is not in the tree: build.sh makes prebuilt archives under
$PHOREPOC_BUILD/thirdparty/{x86,arm}/netsurf/lib/ (NetSurf's core and libraries, curl, libpng, libjpeg, libwebp, FreeType, QuickJS-ng)
and the mbedTLS archive of the tls layer is .../thirdparty/{x86,arm}/lib/libmbedtls.a, where the "thirdparty/..." paths
resolve; the components link them through x32_archives / arm_archives. The run-time resources (NetSurf's Messages and
style sheets, the Choices file, the CA bundle) are staged under .../netsurf/res/ and installed to \resource\phenetsurf\.

The engine ships only as a SIS (recipes/sis.sh); no key here is a ROM component."""
import os

from paths import OS_ROOT

BUILD = os.environ.get("PHOREPOC_BUILD", "/build")
# the patched NetSurf tree of an architecture (recipes/env.sh: $PHOREPOC_BUILD/netsurf-symbian-work/<arch>/netsurf-all)
_NS_LIBS = ("netsurf", "libwapcaplet", "libparserutils", "libhubbub", "libcss", "libdom", "libnsutils", "libnsbmp", "libnsgif",
            "libutf8proc", "libnsfb", "libnslog", "libnspsl", "libsvgtiny")
# link order: the browser core first, the libraries it uses behind it
_LIBS = ("netsurf", "libquickjs", "libcurl", "libmbedtls", "libpng", "libjpeg", "libwebp", "libfreetype", "libcss", "libdom",
         "libhubbub", "libparserutils", "libwapcaplet", "libnsutils", "libnsbmp", "libnsgif", "libutf8proc", "libnsfb",
         "libnslog", "libnspsl", "libsvgtiny")
_RES = ("Messages", "default.css", "internal.css", "quirks.css", "adblock.css", "welcome.html", "credits.html",
        "licence.html", "ca-bundle.crt", "Choices")
_DEFS = ["__SOFTFP=", "nsframebuffer", "small", "STMTEXPR=1", "WITH_CURL", "WITH_NSLOG", "WITH_UTF8PROC", "FB_USE_FREETYPE",
         "_POSIX_C_SOURCE=200809L", "_XOPEN_SOURCE=700", "_BSD_SOURCE", "_DEFAULT_SOURCE"]


def _flags(arch):
    """-I for NetSurf's own headers (the engine core is plain C against them) and the staged libnsfb ones."""
    src = f"{BUILD}/netsurf-symbian-work/{arch}/netsurf-all"
    tp = "x86" if arch == "x32" else "arm"
    dirs = [f"{src}/netsurf", f"{src}/netsurf/include", f"{src}/netsurf/frontends", f"{BUILD}/thirdparty/{tp}/netsurf/include/libnsfb"]
    dirs += [f"{src}/{lib}/include" for lib in _NS_LIBS[1:]]
    out = []
    for d in dirs:
        out += ["-I", d]
    return out


def _archives(arch):
    tp = "x86" if arch == "x32" else "arm"
    return [f"thirdparty/{tp}/netsurf/lib/{a}.a" if a != "libmbedtls" else f"thirdparty/{tp}/lib/libmbedtls.a" for a in _LIBS]


def _res(arch):
    tp = "x86" if arch == "x32" else "arm"
    return [rf"resource\phenetsurf\{f}=thirdparty/{tp}/netsurf/res/{f}" for f in _RES]


COMPONENTS = {
    # The NetSurf engine as a Phorepoc web-engine package: an ECom plug-in (interface 0xE7016B01, webengine repository). The
    # browser frontend discovers and loads it (WebKit's implementation 0xE7016B10 first, then this 0xE7016B11).
    "phewebengine_netsurf": dict(mmp=os.path.join(OS_ROOT, "src/phewebengine_netsurf/group/phewebengine_netsurf.mmp"),
                                 allow_wsd=True, static_ctors=True, cdriver=True, rtti=False,
                                 defs=_DEFS + ["PHE_NS_QJS"], flags=_flags("x32"), rom_data=_res("x32"),
                                 x32_archives=_archives("x32")),
    # The same plug-in on ARM (ARMv6 and up; mbedTLS needs the millisecond clock of entropy_symbian.cpp there).
    "phewebengine_netsurf_arm": dict(mmp=os.path.join(OS_ROOT, "src/phewebengine_netsurf/group/phewebengine_netsurf.mmp"),
                                     rom="phewebengine_netsurf.dll", device=True,
                                     allow_wsd=True, static_ctors=True, cdriver=True, rtti=False,
                                     defs=_DEFS + ["PHE_NS_QJS", "MBEDTLS_PLATFORM_MS_TIME_ALT"], flags=_flags("arm"),
                                     rom_data=_res("arm"), arm_archives=_archives("arm")),
}

# The engine plug-in ships only as a SIS, so it is LOADED FROM RAM, where the loader validates each image (the export
# directory inside the code part; x32: the RIP-relative data references as relocations). `sis` lays it out that way.
for _k in COMPONENTS:
    COMPONENTS[_k].setdefault("sis", True)
