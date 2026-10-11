# BayesTS Studio

A desktop application for specifying, estimating and analysing the Bayesian
time series models of [BayesTS](https://github.com/franzmohr/BayesTS): VARs,
VECs, dynamic factor models, FAVARs and unobserved components models, with
constant or time-varying coefficients.

Built with Qt 6 Widgets and Qwt; runs on Windows, macOS and Linux. Closed
source; see [LICENSING.md](LICENSING.md) for what that requires of the
components it uses.

## What it does today

- Opens a BayesTS model file and shows its tree: groups, datasets and their
  shapes, attributes.
- **Overview**: what `bayests check` says about the model — the
  specification as BayesTS resolves it, whether it will run, and every
  warning (unread datasets, unknown attributes, priors too flat for variable
  selection).
- **Runs** posterior simulation, forecast and log likelihood in-process, on a
  worker thread, with a progress bar and a working Cancel. A run is the run
  the `bayests` command line would make: same front-ends, same per-stage
  seeding from `/model/seed`, single-threaded BLAS unless
  `OPENBLAS_NUM_THREADS` / `OMP_NUM_THREADS` say otherwise.
- **Time-varying coefficients**: for the nine samplers with a coefficient
  path (`VarTvp*`, `VecTvp*`, `DfmTvp*`), the median and 68 % / 90 % bands of
  one coefficient over time, **updating live while the chain runs**, then
  drawn from `/posterior/a/coeffs` once it is written.
- **Draws**: trace, marginal histogram and summary statistics for any row of
  any posterior dataset (double-click it in the navigator).

## Roadmap

1. Model specification without a file: data import (CSV, Excel), choice of
   family, coefficients and error model with tick boxes, priors with
   defaults, validated live against BayesTS before anything is written.
2. Analysis: forecasts with fan charts, impulse responses, FEVD, inclusion
   probabilities, the Savage–Dickey test for time variation, model comparison
   by WAIC / PSIS-LOO across several files.
3. Export: figures (SVG, PDF, PNG), tables, and a script reproducing the
   session.
4. Packaging: signed installers per platform (Windows MSI, macOS notarised
   DMG, Linux AppImage), licence activation.

## Building

BayesTS is pulled in as a CMake subproject (`FetchContent`), pinned by
`STUDIO_BAYESTS_GIT_TAG`. Its dependencies (Armadillo, OpenBLAS, HDF5,
HighFive) are found the same way the BayesTS build finds them; on top of
those this needs Qt ≥ 6.5 and Qwt 6.3 built against Qt 6.

On Windows with MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-qwt-qt6
```

Machine-specific paths go in a gitignored `CMakeUserPresets.json` inheriting
from `default` or `release`, as in BayesTS. Setting
`FETCHCONTENT_SOURCE_DIR_BAYESTS` there builds against a local BayesTS
checkout instead of the pinned commit.

```bash
cmake --preset <your-preset>
cmake --build build/<your-preset>
```
