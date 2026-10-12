# Licensing

BayesTS Studio is free and open-source software under the BSD 3-Clause
License (`LICENSE`), the licence of BayesTS beside it. Everything it is built
from allows that, and allows the binaries to carry the same licence: no
component is GPL-only. This file records why, and the obligations that come
with shipping the components. It is an engineering summary, not legal advice.

## Components

| Component | Licence | Within a BSD-licensed application | Obligation |
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

None restricts the studio's licence or anyone's use of it.

## Fonts

The studio ships its font in its resources (`src/app/fonts`), so that it looks
the same on every system: Inter 4.1 (regular, italic, semibold, bold, bold
italic; Copyright (c) 2016 The Inter Project Authors), sans serif, for the
interface, the assistant's guidance and the figures -- the unmodified files of
the official release, github.com/rsms/inter. It is under the SIL Open Font
License 1.1, which allows bundling it with commercial software provided it is
not sold on its own and keeps its copyright notice and licence: the licence is
installed into `licenses/Inter` and named under Help > Third-party Licences.
Exported PDF figures embed Inter, which the licence allows.

## Example data

`src/engine/data/luetkepohl-e1.csv` and `luetkepohl-e6.csv`, the data of the
two Lütkepohl (2006) worked examples, are data sets E1 and E6 of Lütkepohl,
*New Introduction to Multiple Time Series Analysis* (Springer), as distributed
with JMulTi (`jmulti.de/download/datasets/e1.dat` and `e6.dat`): West German
investment, income and consumption, 1960Q1 to 1982Q4, originally from the
Deutsche Bundesbank (E1); and the German long-term interest rate, from the
Deutsche Bundesbank, and the change of the log GDP deflator, from the DIW,
1972Q2 to 1998Q4 (E6). The files are the values of the series only, retyped
as CSVs with a date column; no code of bvartools, which ships the same
series, is taken. Official statistics of this kind are facts, not a work, and
the textbook and JMulTi publish them for use; the examples cite both.

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

## The studio's own licence

`LICENSE`: BSD 3-Clause, the same text and holder as BayesTS. The setup shows
it for information, both packages install it into `licenses/BayesTS Studio/`,
and Help > Licence opens it. Every source file carries
`SPDX-License-Identifier: BSD-3-Clause` and the copyright line.

What keeps the binaries under it: only LGPL Qt modules, linked dynamically --
a GPL-only module (Qt Charts, Qt Graphs, Qt Data Visualization) would make the
studio as distributed GPL-licensed as a whole -- and no other GPL-only
component. Anyone who rebuilds or changes the studio may replace the Qt
libraries, as the LGPL requires (section "Qt" above).

## The repository

This repository is private and carries no open-source licence. BayesTS stays
an open BSD project: changes to it that the studio needs -- like the
`Reporter::kept_draw()` hook -- are generic embedded-host features and go
upstream in the public repository.
