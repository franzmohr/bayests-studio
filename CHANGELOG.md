# Changelog

Notable changes to BayesTS Studio, newest first. Entries are written for the
people who use the studio, not for whoever wrote the commit.

The studio estimates nothing itself: every draw comes from
[BayesTS](https://github.com/franzmohr/BayesTS), built in at the release
`STUDIO_BAYESTS_GIT_TAG` in `CMakeLists.txt` names. So each version says which
BayesTS release it carries, and an entry that changes a number the studio
computes from the draws -- an impulse response, a decomposition, a spillover,
a multiplier -- says so, and why the new number is the right one. What the
samplers do to the draws is in BayesTS's own CHANGELOG.

Dates are ISO. Versions follow the `project(VERSION)` in `CMakeLists.txt`.

## Unreleased

New entries go here, under an `### Added`, `### Changed` or `### Fixed`
heading, and move down into a version section when one is cut.

## 0.2.0 — 2026-10-22

Built against BayesTS v0.4.0, as 0.1.0 was.

### Added

- **An analysis of the factor models** (Application › Factors) for the four
  DFMs and the FAVAR: the factors' paths with bands, the loadings, the share
  of each series' variance the factors explain, and impulse responses of the
  panel -- and a FAVAR's observed factors -- to the state equation's shocks,
  a FAVAR's identified by a Choleski ordering with the observed factors last.
  Moving loadings, transitions and factor variances are read at a chosen
  period. New numbers, none of which the studio computed before;
  `test/factor_test.cpp` checks each against arithmetic on draws chosen by
  hand.
- **Trend and cycle** (Application › Trend and cycle) for the four
  unobserved components models: each series with its trend and its cycle,
  with bands, and both at a chosen period in a table.
- **The workbench folds the panes that do not apply to the model**: the VAR
  analyses for a factor or UC model, the new panes for a VAR or VEC, the
  multipliers without exogenous variables as before. The application stage
  hides the same tabs.
- **Linux**: CI builds and tests on Ubuntu 26.04, and the studio is packaged
  as a snap, which the release workflow attaches to a release.
- **A homepage**, <https://franzmohr.github.io/bayests-studio/>.

### Fixed

- **A FAVAR's forecast is read at its own width.** Its forecasts carry the
  observed factors after the panel, k + n_obs_factors per horizon; the view
  read k per horizon, so every horizon after the first showed the wrong
  series. The observed factors can now be chosen as well.

## 0.1.0 — 2026-10-15

The first release, built against BayesTS v0.4.0.

### Added

- **BayesTS v0.4.0**, the first release with the hooks the studio uses to
  follow a run, stop it and watch a time-varying coefficient path form.
- **The bvartools workflow as a desktop application**: data, model, priors,
  posterior draws and their application, the five stages of the bvartools
  article, each as a page of its own.
- **The model builder** writes BayesTS model files from imported data: the six
  Gaussian VARs and the six Gaussian VECs, with weakly exogenous variables,
  deterministic terms, BVS and SSVS, and the volatility break of Lenza and
  Primiceri (2022). Each file records the form it was built from, so selecting
  a model fills the form again.
- **Structural analysis**: impulse responses and forecast error variance
  decompositions, orthogonalised and generalised; shocks identified by sign
  restrictions, and by sign and zero restrictions after Arias, Rubio-Ramírez
  and Waggoner (2018). Checked draw by draw against bvartools.
- **Historical decompositions**, as bvartools' `historical_decomposition()`,
  and for time-varying models from each period's own system.
- **Spillovers**: the connectedness measures of Diebold and Yilmaz, as a table
  and over time.
- **Dynamic multipliers** of weakly exogenous variables.
- **Inclusion probabilities** of BVS and SSVS, and a summary plot of the
  coefficient draws, as bvartools' `plot()`.
- **Every result as a matrix**: impulse responses, variance decompositions,
  historical decompositions, spillovers over time and multipliers, a column
  per shock and a row per response. A whole parameter block shows as its
  matrix too, and a time-varying one at a period chosen by its date.
- **Time-varying models at a chosen period**: coefficients, the covariance
  block, impulse responses, variance decompositions and historical
  decompositions.
- **The state variance explorer**: a time-varying model re-estimated at state
  variances of one's choosing, to see what the setting does to the path.
- **The workbench**, the default layout: every stage side by side on a wide
  screen, in panes that fold away one at a time on a laptop and fit their
  content.
- **Exports**: every results table as CSV, LaTeX, Markdown or to the
  clipboard, and every figure as PDF, SVG or PNG.
- **An optional assistant**, off by default (View › Assistant): two workflows,
  a master thesis and a forecasting horse race, with worked examples from the
  bvartools introduction and two papers reproduced step by step, Pinchetti
  (2026) and Jarociński and Karadi (2020). Further examples and palettes load
  from JSON files.
- **Palettes of one's own**, for an organisation's colours, and a choice of
  font for the interface and the figures; the studio ships Inter.
- **A tooltip on every input, button and plot**, and a test that keeps it so.
- **Installers for Windows**: a per-user MSIX that needs no administrator
  rights, and an NSIS setup.exe that installs for the current user or for all
  users.
- **Continuous integration** on GitHub Actions: Windows, MSYS2 UCRT64, Debug
  and Release, against the pinned BayesTS release.

### Fixed

- **The workflow test runs on a clone.** It read bvartools' `e1.csv` from
  `test/reference`, which is generated and not checked in, and failed without
  it; the two checks that compare with it are now skipped by name.
