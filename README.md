# netsurf-symbian

This is [NetSurf](https://www.netsurf-browser.org) running on Phorepoc OS, as a web engine for the Phorepoc browser. It's a
small, fast engine with HTML, CSS, images, https (TLS through mbedTLS) and JavaScript (QuickJS-ng), and it fits devices with
little memory. The browser app finds it through the web-engine plug-in interface once it's installed.

The engine is never part of an OS image. It ships as a SIS file, `netsurf-symbian-x32.sis` for PC images and
`netsurf-symbian-arm.sis` for ARM devices, and you install it from the Catalogue store. The SIS carries the licence texts,
`NOTICE` and the written source offer.

We don't keep a copy of NetSurf here. The recipes download the tree named in `sources.lock` and the libraries it needs, check
each one against its SHA-256, and build them for Phorepoc OS. This repository holds the port itself, the patches and the
recipes.

## Building

NetSurf needs the [tls](https://github.com/Phorepoc/tls) repository for mbedTLS and the certificates the engine trusts, and
the [webengine](https://github.com/Phorepoc/webengine) repository for the plug-in interface. Check out core, the
[pdk](https://github.com/Phorepoc/pdk), tls, webengine and this repository side by side, and run `phe source export -a` once.
Then build the libraries in the toolchain's base image:

```sh
docker run --rm -v "$PWD/..:/src" -e PHOREPOC_PDK=/src/pdk -e PHOREPOC_CORE=/src/core -e PHOREPOC_BUILD=/src/core/build \
    -e PHOREPOC_ADAPTATIONS=/src/tls:/src/webengine:/src/netsurf-symbian \
    -e PHOREPOC_ADAPTATION_NAMES=tls:webengine:netsurf-symbian \
    ghcr.io/phorepoc/base bash /src/netsurf-symbian/recipes/prebuild.sh
```

Add `-e PHOREPOC_ARCH=arm` for an ARM device. Once that's done, build the component:

```sh
export PHOREPOC_ADAPTATIONS=<device repositories>:<tls>:<webengine>:<netsurf-symbian>
phe component build -b myboard phewebengine_netsurf
```

For an ARM board, that's `phe component build -b myboard -arch arm phewebengine_netsurf_arm`. The pdk's `package.yml` does
all of this on every release (`.github/workflows/release.yml`), and `recipes/sis.sh` makes the SIS.

## What's in here

- `os/src/phewebengine_netsurf/` is the plug-in: `phewebengine_netsurf.cpp` is the ECom adapter, and `phengine_ns.c` drives
  NetSurf on its own thread and draws pages into a bitmap the browser shows.
- `os/src/netsurf/` has the header that closes a few Open C gaps in every NetSurf file.
- `patches/` are our changes to the NetSurf tree: platform guards for Symbian's libc, the resource path of the engine, a
  stack size for QuickJS and a fix for `document.title`.
- `os/data/netsurf/` has the `Choices` files, which name the fonts of the Phorepoc images and the CA bundle.
- `recipes/` downloads and builds NetSurf and the libraries it needs: curl, libpng, libjpeg, libwebp, FreeType and QuickJS-ng.
- `registry/` has the component entries the pdk builds.

## License

Our files are GPL-2.0-only, the license of NetSurf, because the engine links it into one DLL. The other libraries keep their
own licenses, which `NOTICE` lists.

Contributions are welcome under the same terms. Add your name to `AUTHORS` in your first pull request.
