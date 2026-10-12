# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# The horse race's scores recomputed by bvartools from the same files.
#
#   Rscript test/horserace_reference.R test/reference/horserace
#
# Run after studio_horserace_test, which writes the race and its scores.csv.
# For every candidate and origin the model is read back with
# read_model_from_hdf5(); the forecast errors are bvartools'
# add_forecast_errors() (realised less every forecast draw), whose mean over
# draws is the error of the posterior mean forecast, and the log score is
# selection_criteria()'s log predictive likelihood. Prints the largest
# difference from the studio's scores and fails above 1e-10.

suppressPackageStartupMessages(library(bvartools))
folder <- commandArgs(TRUE)[1]
manifest <- jsonlite::fromJSON(file.path(folder, "horse-race.json"), simplifyVector = FALSE)
ours <- read.csv(file.path(folder, "scores.csv"), stringsAsFactors = FALSE)
k <- length(manifest$variables)
h <- manifest$horizon
worst <- 0
for (c in seq_along(manifest$candidates)) {
  files <- unlist(manifest$candidates[[c]]$files)
  squared <- matrix(0, k, h)
  count <- matrix(0, k, h)
  for (o in seq_along(files)) {
    model <- add_forecast_errors(read_model_from_hdf5(file.path(folder, files[o])))
    e <- colMeans(model$posterior$forecast$errors)     # variable within horizon
    observed <- length(e) / k
    squared[, seq_len(observed)] <- squared[, seq_len(observed)] + matrix(e^2, k)
    count[, seq_len(observed)] <- count[, seq_len(observed)] + 1
    lpl <- selection_criteria(model)$LPL$mean
    studio <- ours$value[ours$candidate == c & ours$measure == "logscore" & ours$origin == o]
    worst <- max(worst, abs(lpl - studio))
  }
  rmse <- sqrt(squared / count)
  for (i in seq_len(k)) {
    for (j in seq_len(h)) {
      studio <- ours$value[ours$candidate == c & ours$measure == "rmse" &
                             ours$variable == manifest$variables[[i]] & ours$horizon == j]
      worst <- max(worst, abs(rmse[i, j] - studio))
    }
  }
}
cat(sprintf("horse race: largest difference from bvartools %.2e\n", worst))
if (worst > 1e-10) quit(status = 1)
