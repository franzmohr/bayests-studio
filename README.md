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

- **Structural analysis** (Application › Structural analysis): impulse
  responses of every variable to a chosen shock -- forecast error,
  orthogonalised and generalised for a reduced form model, structural and
  structural generalised for a structural one, as bvartools allows them --
  with median and 68 % / 90 % bands, cumulative or not, at a chosen period
  where coefficients or volatilities move; and the forecast error variance
  decomposition of a chosen variable for orthogonal shocks (oir, sir), as the
  posterior mean share of each shock. VECs are rewritten as the level VAR
  their draws imply. Computed on a worker thread.

  The definitions are bvartools' (`irf()`, `fevd()`, `src/ir.cpp`,
  `src/vardecomp.cpp`, `R/collect_draws.R`). `test/structural_test.cpp`
  holds the studio to them draw by draw on files bvartools estimated and
  wrote (`test/structural_reference.R`): a VAR(2), a structural VAR, the
  article's VEC on e6 and a TVP-VAR with stochastic volatility -- every
  response draw and every decomposition within 5e-15. The generalised
  decompositions (gir, sgir) are left out until the open question of their
  normalisation in bvartools is settled.

- **Sign restrictions** (Structural analysis › Sign Restrictions…): a grid
  per horizon -- responses in rows, shocks in columns, a tab each for impact,
  any horizon added and the long run -- whose cells are clicked or typed to
  +, − or 0 and left blank where free; and a budget of tries per draw; for every draw a rotation of the Choleski factor is drawn
  uniformly until the restrictions hold (bvartools'
  `add_sign_restrictions()`: QR of a normal matrix, R's signs moved into Q,
  a failing shock retried with its column negated). The identification is
  written as bvartools writes it -- `/model/sign_restrictions/restrictions`,
  `/posterior/q/coeffs` -- so R reads it back with the model, and a file
  bvartools identified opens with its identification. Impulse responses and
  the variance decomposition then offer "Sign restrictions", over the
  identified draws, with unrestricted shocks marked as having no
  interpretation. Seeded from `/model/seed` on a stream of its own.

  Verified in `test/structural_test.cpp`: on bvartools' rotations the
  studio's responses and decomposition agree with `irf(type = "sign")` and
  `fevd(type = "sign")` within 6e-15; the studio's own search accepts only
  orthonormal rotations (|Q'Q - I| ~ 1e-15) with no restriction violated,
  and its median responses sit within Monte Carlo error of bvartools' (0.08
  of the 68 % band); and bvartools, reading the studio's identification,
  reproduces the studio's responses within 5e-15.

- **Sign and zero restrictions** (the same dialog, with a 0 in a cell or a
  long-run tab): Arias, Rubio-Ramírez and Waggoner (2018) as
  bvartools' `add_sign_zero_restrictions()` implements it -- each draw's
  rotation built column by column on the null space of its zero
  restrictions (Algorithm 2), weighted by the ratio of the closed-form and
  the numerically differentiated volume elements (Algorithm 3), the weights
  Pareto smoothed (Vehtari et al. 2024), more tries per draw with the
  Girshick–Mosteller–Savage estimate of the acceptance probability, and the
  posterior resampled by the weights. The dialog reports the accepted draws,
  the effective sample size and the Pareto shape, and bvartools' warnings
  where the importance sample is unreliable. **The resample is written to a
  new model file** beside the original, whose chain is left as it was, in
  bvartools' layout (`/model/sign_zero_restrictions`, plus
  `/model/sign_restrictions` and `/posterior/q/coeffs` for the responses);
  the new model joins the project and opens on its identification.

  Verified in `test/structural_test.cpp` against bvartools
  (`test/signzero_reference.R`): the Pareto smoother exactly (7e-14, the
  shape to 1e-10); responses and decompositions from bvartools' sign and
  zero rotations exactly (4e-15); under signs alone the two volume elements
  cancel to 1e-9, which checks the numerical one by an independent route;
  the studio's own identification of bvartools' example accepts 200 of 1000
  draws (bvartools 216) with an effective sample size of 167 (181), Pareto
  k -0.07 (-0.10), its zero restriction exact to 4e-16 and no sign violated,
  responses within Monte Carlo error; and bvartools, reading the studio's
  file, reproduces its responses within 2e-15.

- **Spillovers** (Application › Spillovers): the connectedness measures of
  Diebold and Yilmaz (2012, 2014) as bvartools' `spillover()` computes them --
  generalised (Pesaran and Shin 1998, by the shock's variance, rows
  normalised), Choleski, or from a sign identification's rotations; the H
  step error made of the responses of periods 0 to H - 1; every measure
  computed per draw and only then summarised. Four views: the table with its
  from column, to row and total index; the directional measures (net, to,
  from) with 90 % intervals; the net pairwise table; and, for a model whose
  coefficients or covariance move, **the total index in every period of the
  sample** with 68 and 90 % bands -- what Diebold and Yilmaz get from rolling
  windows, from one estimation. A VEC is decomposed as its level VAR; a
  structural model is refused, as in bvartools.

  Verified in `test/structural_test.cpp` against bvartools
  (`test/spillover_reference.R`), every draw's total, from, to and net and
  the mean table and net pairwise: generalised and Choleski on the VAR(2),
  the sign identification, the VEC, and the TVP-SV model at the last and the
  tenth period, all within 1.1e-13.

- **Summary plot** (Posterior simulation › Summary plot): bvartools'
  `plot()` of a model -- one grid per block of coefficients, a row per
  equation and a column per regressor, each panel a histogram (Sturges on
  pretty breaks, as `hist()`), trace or box plot (Tukey's hinges and
  whiskers, as `boxplot()`) of the draws, and for a coefficient or covariance
  that moves its median and credible band over the sample. The blocks are
  bvartools': lagged endogenous variables, exogenous variables and
  deterministic terms for a VAR; the cointegration matrix Π = αβ′, lagged
  differences, differenced exogenous variables and unrestricted
  deterministic terms for a VEC; A₀ of a structural model, its ones and
  zeros as values; and the error covariance. A block wider than six columns
  is split into pages of nearly equal width, as `max_cols` splits it.

  Verified in `test/structural_test.cpp` against bvartools' `summary()`
  (`test/summary_reference.R`): every panel's mean, median and 95 % bounds,
  in summary()'s order, on the VAR(2) (30 panels, 4e-14), the VEC with Π
  (22, 3e-15) and the structural VAR with A₀ (39, 4e-14).

- **Weakly exogenous variables** (the model builder's second list): series
  the model does not explain, at lags 0 to s -- levels for a VAR, and for a
  VEC differences at lags 0 to s - 1 with the level at lag 1 in the
  cointegration space (s a level order, as bvartools has it). A forecast is
  conditional on their values over the horizon, so with exogenous variables
  the horizon is bounded by the data after the sample and the periods held
  out. Under the Minnesota prior their coefficients get bvartools'
  κ₁κ₂/(l + 1)² σᵢ²/sⱼ².

  Verified twice. In `test/structural_test.cpp`, the builder's y, z, w and
  forecast regressors for a VARX and a VECX are those bvartools'
  `create_bvarmodel()`/`create_bvecmodel()` and `add_forecast_input()`
  write for the same specification (`test/exogenous_reference.R`), within
  2.5e-14. In `test/builder_test.cpp`, all twelve VAR and VEC algorithms with
  an exogenous variable pass `bayests check` without a warning and run every
  stage, forecast and score included.

- **Dynamic multipliers** (Application › Multipliers): the response of every
  endogenous variable to a change in one exogenous variable, permanent (held
  from period 0) or transitory, of any size, cumulated or not, at any period
  of a time-varying model -- bvartools' `multipliers()`. A VEC is read as its
  level VAR (B₀ = Υ₀, B₁ = Πₓ − Υ₀ + Υ₁, …, as BayesTS's `vec_to_var.h`), so
  its cointegration relations move the levels for good. Held to bvartools
  draw by draw, permanent and transitory: the VARX, the VECX and a TVP-SV
  VARX at period 20, within 6e-15. The sign and zero restrictions now read a
  VEC with exogenous variables in the same level layout.

- **Historical decompositions** (Application › Historical decomposition):
  bvartools' `historical_decomposition()` -- each draw's structural shocks
  e_t = P⁻¹u_t, Choleski or sign-identified, propagated through the sample,
  M_t = Σ A_l M_{t−l} + P diag(e_t), and the baseline whatever of the data
  they leave (deterministic terms, exogenous variables, initial
  conditions). Drawn as bvartools' `plot.bvarhd()` draws it: the posterior
  mean or median contribution of each shock stacked as bars, positive parts
  above zero and negative below, their sum as a line, the baseline on
  request (and the line is then the data). A VEC is decomposed in levels,
  from the levels its differences and error correction term imply. Constant
  coefficients and covariance only, and no structural model, as bvartools.

  Verified in `test/structural_test.cpp` against bvartools
  (`test/hd_reference.R`), every period's mean, median and 90 % bounds of
  every contribution and the baseline: the VAR(2), its sign identification,
  the VEC, the VARX and the VECX, within 4e-13; and again from z alone, the
  only regressors a file of the builder carries, identically.

- **Inclusion probabilities** (Posterior simulation › Inclusion
  probabilities): for a model estimated with BVS or SSVS, the share of draws
  in which each coefficient is in the model, as bvartools' `summary()`
  reports it -- a table per block, a row per equation and a column per
  regressor, shaded by the probability and in bold from one half on (the
  median probability model of Barbieri and Berger 2004), and the covariance
  block's Ψ where it was selected over too. What selection did not reach is
  greyed out with the reason: a VEC's cointegration matrix (the loadings are
  never selected), terms left out of it, A₀'s fixed entries.

  Verified in `test/structural_test.cpp` against bvartools' `summary()`
  (`test/inclusion_reference.R`): SSVS on the VAR(2), BVS with a selected
  covariance block, BVS on a structural VAR and on a VEC, every probability
  exactly.

- **Table export**: every results table -- the model comparison, the
  inclusion probabilities, the connectedness and net pairwise spillover
  tables -- has an Export button and a context menu (and Ctrl+C). Copy puts
  it on the clipboard as tab separated text and HTML, so Excel or Word
  pastes a table; Copy as LaTeX and Copy as Markdown give the source; Save
  writes CSV (UTF-8 with a byte order mark, for Excel), LaTeX or Markdown,
  and the inclusion page saves all its tables in one file. The LaTeX is a
  booktabs table with caption, label and the view's notes beneath:
  numeric columns set right, numbers' minus signs as minus signs, the bold
  and grey of the view kept, LaTeX's special characters escaped and the
  Greek letters, arrows, subscripts and stars the views print set as math.
  `test/table_test.cpp` reads the CSV back cell for cell and compiles the
  LaTeX with pdflatex where one is on PATH.

- **Figure export**: every figure -- impulse responses, the variance
  decomposition, spillovers, multipliers, the historical decomposition, the
  summary plot, the time-varying bands, traces and the data and forecast
  plots -- has a right-click menu, and the analysis views an Export Figure
  button: Copy Figure (an image for a document), Save Figure As… PDF
  (vector, for `\includegraphics`), SVG or PNG at a chosen resolution, and
  for a grid Save This Panel As…. A grid exports whole, with its title, row
  and column labels. A figure is drawn 16 cm wide by default -- a LaTeX
  article's text width -- with 9 pt text, in black on white whatever the
  application's theme. Numbers everywhere, on screen too, now use a
  decimal point and no group separator, as the tables and R do.

## Roadmap

1. The builder for the remaining families: the quantile and discounted
   models, `VecKlgs2010`, factor models and FAVARs, unobserved components;
   seasonal dummies; Excel import; an R-side reader (`rclass`) so a studio
   file opens as a bvartools object.
2. Analysis: the Savage–Dickey test
   for time variation, PSIS-LOO, expanding windows.
3. Export: a script reproducing the session.
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
