# BayesTS Studio

**Homepage, screenshots and downloads:
<https://franzmohr.github.io/bayests-studio/>**

A desktop application for the Bayesian time series models of
[BayesTS](https://github.com/franzmohr/BayesTS). It specifies VARs and VECs,
with constant or time-varying coefficients, in its model builder; and it
estimates and analyses those and the dynamic factor models, FAVARs and
unobserved components models a model file describes.

Built with Qt 6 Widgets and Qwt; built and tested on Windows and Linux. Free and
open source under the BSD 3-Clause License, as BayesTS is; see
[LICENSING.md](LICENSING.md) for the licences of the components it uses.

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
  Appendix A), the random-walk and error priors, a volatility break for the
  constant-coefficient Wishart models (Lenza and Primiceri 2022: from a
  chosen period every shock's variance is scaled, a few free scales and then
  a decay, with the paper's priors), the chain, the seed, the forecast
  horizon and a holdout for scoring are all form controls. The
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

- **Colour palettes** (View › Colour Palette…): the series of every plot and
  every exported figure in the studio's Okabe–Ito default, the five most
  used palettes of ggplot2 (its hue palette; ColorBrewer's Set1, Set2, Dark2
  and Paired), of matplotlib (tab10, tab20, tab20b, tab20c, petroff10) and
  of the viridis family (viridis, magma, inferno, plasma, cividis), an
  extension's -- or, under My organisation's colours, as many palettes of your
  own as there are house styles to follow, each made from the one selected,
  renamed, deleted and edited there (an earlier version's single palette is
  the first of them). `STUDIO_SETTINGS=<folder>` keeps the settings in an
  INI file there instead of the registry, for a portable install. The values are generated from the packages
  themselves by `tools/make_palettes.py`; their licences, none restrictive,
  are in LICENSING.md and the Third-party Licences box.

- **Extensions** (Help › Extensions…): worked examples and colour palettes as
  JSON files, without a rebuild. The studio reads every `*.json` in the user's
  extensions folder (or the folder `STUDIO_EXTENSIONS` names, for a lab or a
  portable install) and the `studio-example.json` of every reproduction
  project, so a paper added there with one is offered by the assistant. An
  example with a built-in's id replaces it; race candidates are named by key
  (`minnesota`, `sv`, `one-lag`, `vec`, `break`, ...); data paths are
  relative to the file. Unknown keys and notes for steps a workflow lacks are
  reported, and a file with an error gives nothing and says why. Write a
  Template puts a working example and a README of every field in the folder.
  `engine/Extensions`; `workflow_test` round-trips every built-in example.

- **Volatility break** (Posterior simulation › Volatility break): for a
  model estimated with one, the path of the scale s_t from before the break
  to the end of the forecast horizon, median with 68% and 90% bands; the
  posterior of the decay rate ρ against its beta prior; and a table of the
  free scales and ρ with their priors -- all exportable.

- **Fonts** (View › Font…): the interface's font, the system's by default,
  and the one plots and exported figures set their text in -- the
  interface's family, or another, such as the typeface of the document the
  figures go into -- with the size of an exported figure's text.

- **Workbench** (View › Layout, the default): every stage side by side on
  a wide screen -- the specification and the series on the left, the
  priors, the posterior draws, impulse responses above the historical
  decomposition, and spillovers, multipliers and forecasts. A
  model the builder wrote fills the form in with its own data and
  settings; Estimate saves the form as a new model and runs it. Every pane
  folds from its title, and its corner button shows it alone, the others
  folded -- a column of folded panes becomes a narrow strip, so a laptop
  works with one or two at a time (View › Layout › Unfold All Panes brings
  them back). The stage bar, one page at a time, is the other layout.

- **State variance** (Posterior simulation › State variance): a
  time-varying model estimated again at a state variance you name, each
  run's path of a coefficient drawn over the model's own, with the prior
  state variance against the posterior one.

- **Assistant** (optional, off by default; View › Assistant turns it on,
  and with it the Assistant menu and the dock on the right): a workflow as
  steps, each with what to do, why and the usual choices with their
  references, buttons that do it, and the stage it concerns opened beside
  it. A step ticks itself off when the project shows it done -- data
  imported, a model estimated, a log likelihood written -- or when you
  confirm a judgement only you can make, and the assistant moves on. What it
  did is kept in the project's `assistant.json`, so a project reopens where
  it was left.
  - **Introductory examples** -- research question and data, a
    look at the data, a baseline BVAR (Minnesota prior, a year of lags, a
    fixed seed), estimation, convergence, the lag order compared by WAIC
    (the alternatives made and estimated for you), identification,
    impulse responses and variance decompositions, robustness checks (the
    reverse order, a looser and a tighter prior, the later sample, made and
    estimated for you), and the write-up: every figure as PDF, every table
    as LaTeX, a draft methods paragraph stating the model, sample, prior,
    sampler and identification, and a `results.tex` that inputs them all.
  - **Forecast horse race** -- data, the design (variables, the windows,
    forecast origins, horizon, and the field: a Minnesota BVAR benchmark,
    the same with stochastic volatility, a TVP-VAR, variable selection, a
    VAR(1), a VEC for levels, and -- given its date -- the benchmark with the
    volatility break of Lenza and Primiceri (2022), which windows ending
    before the break estimate without it), the run, the league table and
    when a model wins, and the report. Every model is estimated at every
    origin on an expanding window, the runs as `bayests` processes several
    at a time (a stopped race resumes), all candidates at an origin from the
    same seed, so models that coincide there draw identically, and scored by
    the RMSE of the posterior mean
    forecast relative to the benchmark and the log predictive score, whose
    cumulative difference over the origins the Evaluation stage's Horse race
    tab plots. `test/horserace_test.cpp` runs a race end to end and
    `test/horserace_reference.R` recomputes every score with bvartools from
    the same files: 1.8e-15.
  - **Worked examples**: Lütkepohl's (2006) VAR, as the bvartools
    introduction runs it, and his VEC of the interest rate and inflation
    (data set E6), as the bvartools article runs it -- both on data the
    studio ships; and, from the
    reproduction projects (a folder the assistant asks for once, then
    remembers), the papers under "Reproducing papers", for which the
    assistant prepares the data from the replication files, fills in the
    specification and identification, and says at every step what the paper
    does there and what to expect -- Arias, Rubio-Ramírez and Waggoner
    (2018), Nam and Wang (2018), Leduc and Liu (2016), Jarociński and
    Karadi (2020) and Pinchetti (2026) for structural analysis, the last two
    with their high-frequency surprises as equations without coefficients,
    their own Minnesota priors and two shocks rotated, Jarociński and
    Karadi's missing surprises drawn every sweep and Pinchetti's
    restrictions with thresholds; forecasting through the pandemic on Lenza
    and Primiceri's (2022) data for the horse race. The papers the studio
    cannot yet implement step by step -- ESRB (2026), Lenza and Primiceri's
    hierarchical prior and scenarios -- are listed with what they need.

- **Factor models** (Application › Factors, for the four `Dfm*` models and
  `FavarNormalWishart`): each unobserved factor's path with 68 % / 90 %
  bands; the loadings, posterior median and 90 % interval; the share of each
  series' variance the factors explain, R² of its common component over the
  sample; and the impulse responses of the panel -- and a FAVAR's observed
  factors -- to the state equation's shocks: one standard deviation of a
  DFM's independent factor innovations, or a FAVAR's Choleski shocks with
  the observed factors ordered last (Bernanke, Boivin and Eliasz 2005). A
  model whose loadings, transition or factor variances move is read at a
  period chosen by its date. A FAVAR's forecast shows its observed factors
  too.
- **Trend and cycle** (Application › Trend and cycle, for the four `Uc*`
  models): each series with its trend, and its cycle -- the output gap of a
  GDP series -- with bands over the sample, and both at a chosen period in a
  table. The cycle is the model's own draws beside an irregular component,
  and the data less the trend otherwise.

  The VAR and VEC analyses are hidden for these models, and the workbench
  folds the panes that do not apply to the model selected.
  `test/factor_test.cpp` checks every number on files whose draws are chosen
  by hand.

## Roadmap

1. The builder for the remaining families: the quantile and discounted
   models, `VecKlgs2010`, factor models and FAVARs, unobserved components;
   seasonal dummies; Excel import; an R-side reader (`rclass`) so a studio
   file opens as a bvartools object.
2. Analysis: the Savage–Dickey test
   for time variation, PSIS-LOO, expanding windows.
3. Export: a script reproducing the session.
4. Packaging: signed installers per platform (Windows: an MSIX for managed
   machines, see `packaging/msix/README.md`, and an NSIS setup.exe for
   students, see `packaging/nsis/README.md`; macOS notarised DMG, Linux
   AppImage).

## Building

BayesTS is pulled in as a CMake subproject (`FetchContent`), pinned by
`STUDIO_BAYESTS_GIT_TAG` to the release `v0.4.0`, the first with the hooks
the studio needs to follow, stop and watch a run. Its dependencies (Armadillo, OpenBLAS, HDF5,
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

### Linux

What the Linux CI job does, on Ubuntu 26.04 or anything else with Qt ≥ 6.5
(Ubuntu 24.04's apt stops at 6.4). No distribution packages Qwt for Qt 6, so
it is built first, into a prefix of your choosing, by the script CI uses; it
pins Qwt's version and checks the tarball's checksum.

```bash
sudo apt install build-essential gfortran cmake ninja-build pkg-config curl git \
  qt6-base-dev qt6-base-dev-tools qmake6 qt6-svg-dev libgl-dev \
  libarmadillo-dev libopenblas-dev libhdf5-dev
.github/scripts/build-qwt.sh ~/.local/qwt
```

HighFive is header-only; clone it at the tag the pinned BayesTS release names
in its `.github/highfive-version` (`v3.0.0-beta2` for v0.4.0):

```bash
git clone --depth 1 --branch v3.0.0-beta2 https://github.com/BlueBrain/HighFive.git ~/.local/highfive
```

Then configure with both, by hand or in a `CMakeUserPresets.json`:

```bash
cmake --preset release -DHIGHFIVE_DIR=$HOME/.local/highfive -DQwt_ROOT=$HOME/.local/qwt
cmake --build build/release
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ctest --test-dir build/release
```

The executable is `build/release/src/bayests-studio`, with `bayests` copied
beside it. Without a display, set `QT_QPA_PLATFORM=offscreen` for the tests.

The snap (`snap/snapcraft.yaml`) builds the same thing on core24 against the
Qt of the kde-neon-6 runtime:

```bash
snapcraft
sudo snap install --dangerous ./bayests-studio_*.snap
```

## Citing BayesTS Studio

[CITATION.cff](CITATION.cff) is the machine-readable version, and is what
GitHub's "Cite this repository" box reads. Cite the version you used:

> Mohr, F. X. (2026). *BayesTS Studio*. Version 0.1.0. Zenodo.
> <https://doi.org/10.5281/zenodo.23388739>

That is the **version** DOI, which resolves to the exact release. The
**concept** DOI [10.5281/zenodo.23388738](https://doi.org/10.5281/zenodo.23388738)
always resolves to the newest one. The numbers come from BayesTS, so cite the
release the studio builds on as well -- 0.1.0 builds on BayesTS 0.4.0,
<https://doi.org/10.5281/zenodo.23360540> -- and the methods behind the
models you estimated.
