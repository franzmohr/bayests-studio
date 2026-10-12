# CLAUDE.md

BayesTS Studio: a closed-source Qt 6 Widgets desktop application over the
BayesTS library (D:/projects/BayesTS, pulled in by FetchContent; the user
presets point `FETCHCONTENT_SOURCE_DIR_BAYESTS` at the local checkout).

- **Licensing constrains the dependencies** -- read LICENSING.md before adding
  one. Only LGPL Qt modules, linked dynamically; never Qt Charts, Qt Graphs or
  another GPL-only module. Plots are Qwt.
- **The window follows the workflow of the bvartools JSS article** (Section 3,
  Figure 1): Data, Model set-up, Posterior simulation, Evaluation,
  Application -- `StageId` in `src/views/StagePages.h`, one page each. A
  project is a folder of model files, compared side by side (Figure 2).
- **A run must be the run `bayests` would make.** `engine/ModelRunner` creates
  the registered front-end, seeds each stage with `seed_model_stage()` on the
  worker thread (Armadillo's RNG is thread-local) and hands it the
  `QtReporter`. Do not reimplement a sampler stage here.
- Generic needs of an embedded host (progress, cancellation, live draws via
  `Reporter::kept_draw()`) go upstream into BayesTS, not into a fork.

Build: `cmake --preset <your-preset>`, `cmake --build build/<your-preset>`.
Runtime PATH on this machine:
`/d/vcpkg/installed/x64-mingw-dynamic/bin:/d/HDF5/2.0.0/bin:/c/msys64/ucrt64/bin`.

Smoke test without a display: `QT_QPA_PLATFORM=offscreen
STUDIO_SMOKE_CAPTURE=<prefix> bayests-studio <file-or-folder>` runs every
stage of the first model and saves a capture per stage as
`<prefix>-stage<n>.png`.

**Packaging.** `cmake --install <build> --prefix <dir>` lays out a
self-contained copy (`cmake/StudioInstall.cmake`): both executables, every
DLL, Qt's plugins, `licenses/`. The Windows package is that layout plus a
manifest, packed by `packaging/msix/build-msix.ps1`; see the README there.
Qt Network and its plugins are kept out on purpose -- the application makes
no network connection, and a new dependency that needs them is a decision,
not a fix. Test a layout by running it with only `C:\Windows\System32` on
PATH; it does not contain the `offscreen` platform, so the smoke capture runs
with a visible window there.
