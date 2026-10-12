# Licensing

BayesTS Studio is intended to be sold as a closed-source commercial
application. Everything it is built from has been chosen so that this is
possible without buying a licence for any component. This file records why,
and the obligations that come with it. It is an engineering summary, not legal
advice: have it reviewed before the first sale.

## Components

| Component | Licence | Closed-source use | Obligation |
| --- | --- | --- | --- |
| BayesTS (samplers, model-file layer) | BSD 3-Clause | Yes | Ship its copyright notice and licence text |
| Qt 6: Core, Gui, Widgets, Svg, Concurrent, PrintSupport, OpenGL | LGPL v3 | Yes, **if linked dynamically** | See "Qt" below |
| Qwt 6.3 | Qwt License 1.0 (LGPL 2.1 + exceptions) | Yes; the exceptions also permit static linking | Ship the licence; changes to Qwt itself must be published |
| Armadillo | Apache 2.0 | Yes | Ship the licence and NOTICE |
| OpenBLAS | BSD 3-Clause | Yes | Ship the licence |
| HDF5 | BSD-style (The HDF Group) | Yes | Ship the copyright notice |
| HighFive | Boost 1.0 | Yes | None for binaries |
| GCC runtime (libstdc++, libgomp, libgfortran) | GPL 3 + GCC Runtime Library Exception | Yes | None for binaries built with GCC |
| Microsoft C runtime (`vcruntime140.dll`) | Visual Studio redistributable ("Distributable Code") | Yes, app-local | Comes in only because the HDF5 build on this machine is an MSVC one; a MinGW HDF5 removes it |

The Windows package (`packaging/msix/`) installs `licenses/<component>/`
from wherever each dependency came from (`cmake/StudioInstall.cmake`); a text
that cannot be found is a configure-time warning, so read those before
shipping.

## Colour palettes

The palettes of View › Colour Palette are colour values only, generated into
`src/views/PaletteData.inc` by `tools/make_palettes.py` from the packages
below; colour values carry no code, but each source asks for its notice, so
the About box and the shipped licence file reproduce them.

| Palettes | Source | Licence | Obligation |
| --- | --- | --- | --- |
| Hue (ggplot2's default) | scales (R), `hue_pal()` | MIT | Ship the copyright notice |
| Set1, Set2, Dark2, Paired | ColorBrewer by Cynthia A. Brewer, via RColorBrewer | Apache 2.0 | Ship the licence and the attribution "Colors from www.ColorBrewer.org by Cynthia A. Brewer, Geography, Pennsylvania State University" |
| tab10, tab20, tab20b, tab20c, petroff10 | matplotlib | Matplotlib licence (PSF-style, BSD-compatible) | Retain "Copyright (c) 2012- Matplotlib Development Team" and the licence |
| viridis, magma, inferno, plasma, cividis | viridisLite (R); the first four by Nathaniel Smith, Stéfan van der Walt and Eric Firing, released CC0; cividis by Nuñez, Anderton and Renslow (2018) | MIT (viridisLite) | Ship the copyright notice |

None restricts closed-source or commercial use.

## Qt

Under the LGPL the application may stay closed, provided that:

- Qt is linked **dynamically** (the default here) so that a user can replace
  the Qt libraries with their own build;
- the licence text is shipped and the use of Qt is stated (Help › Third-party
  licences does this);
- the Qt source code for the exact version shipped is made available, or a
  written offer of it is; linking to download.qt.io is the usual practice;
- nothing in the product or its licence terms prevents replacing Qt
  (no reverse-engineering ban covering it, no lock-down of the installed DLLs).

**Do not add a GPL-only Qt module.** Qt Charts, Qt Graphs, Qt Data
Visualization, Qt Virtual Keyboard and Qt Quick 3D Physics, among others, are
available only under the GPL in the open-source edition, and using any one of
them would require either releasing the application under the GPL or a
commercial Qt licence. This is why plots are drawn with Qwt.

A commercial Qt licence (or Qt for Small Business) removes the LGPL obligations
and allows static linking and the GPL-only modules. It can be bought later
without changing any code.

## The repository

This repository is private and carries no open-source licence. BayesTS stays
an open BSD project: changes to it that the studio needs -- like the
`Reporter::kept_draw()` hook -- are generic embedded-host features and go
upstream in the public repository.
