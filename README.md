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

- **Model builder** (Data › Import Data…, then New Model from These Data…):
  a CSV or other delimited file becomes a model file without writing any
  code. Endogenous variables are ticked and ordered; the sample range,
  lags, constant and trend, constant or time-varying coefficients
  (optionally non-centred, for the test for time variation), Wishart, gamma
  or stochastic-volatility errors, the covariance block, a structural form,
  BVS or SSVS, the coefficient prior (or the Minnesota prior of the article's
  Appendix A), the random-walk and error priors, the chain, the seed, the
  forecast horizon and a holdout for scoring are all form controls. The
  defaults are those of the bvartools article's replication script.
  Combinations BayesTS refuses are disabled, and every change is written to
  a scratch file and run through `bayests check`, whose verdict the dialog
  shows before anything is saved. Starting values are least squares, as
  `add_initial_values(method = "ols")` sets them. Coefficient and variable
  names travel in the file as attributes no reader looks at, so every view
  says "income ← invest(-1)" rather than "row 4".

  Covered so far: the twelve Gaussian VARs and VECs. For a VEC the form adds
  the cointegration rank, whether the constant and the trend sit inside the
  cointegration space, and its prior -- Koop, León-González and Strachan
  (2010) for a constant space, the state equation with autocorrelation ρ for
  a time-varying one. Its chain starts at Johansen's maximum likelihood
  estimate of beta, scaled to the state equation where the space moves, as
  bvartools starts it.

  Files follow the conventions of bayests-r's `write_bayests_tree()`:
  `/data/train/y` and `/data/test/y` are time series with `variables` and
  `tsp`, so `read_bayests_tree()` gives R a named `ts` with its dates; the
  seed is an R integer. BayesTS reads that layout of `y` exactly as the
  stacked one (identical draws).

  `test/builder_test.cpp` writes all 92 combinations the form allows -- a
  simulated VAR for the VAR row, a simulated cointegrated system for the VEC
  row, across ranks and deterministic cases -- checks each with
  `bayests check` (no refusal, no warning), runs all three stages, holds a
  flat-prior VAR(1) to the simulated coefficients and to least squares, and
  a VEC to the simulated cointegration space (β normalised: 1, −1.003, 0.003
  against 1, −1, 0) with a forecast in levels.

## Roadmap

1. The builder for the remaining families: the quantile and discounted
   models, `VecKlgs2010`, factor models and FAVARs, unobserved components;
   exogenous variables and seasonal dummies; Excel import; an R-side reader
   (`rclass`) so a studio file opens as a bvartools object.
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
