> Small as a mouse, fast as a cheetah and available for free. NetSurf is a multi-platform web browser for RISC OS,
> UNIX-like platforms (including Linux), Mac OS X, and more.
>
> -- [NetSurf](https://www.netsurf-browser.org/)

The Phorepoc browser draws the page with a web engine that you install separately. This package is NetSurf's engine,
with its own HTML and CSS layout and QuickJS for JavaScript, built for Phorepoc as one plug-in library the browser
finds and loads at run time. Install it, open Browser and type an address. It needs far less memory than WebKit, which
suits older phones.

No engine is part of any Phorepoc image: engines come only from Catalogue, so you can pick, update or remove one.
NetSurf is GPL 2; the package installs every licence text and the source offer in `\resource\phweb\netsurf\licences\`.
