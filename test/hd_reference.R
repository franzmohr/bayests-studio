# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# Reference historical decompositions from bvartools, for
# test/structural_test.cpp to hold the studio's to, period by period.
#
#   Rscript test/hd_reference.R test/reference
#
# Run after test/structural_reference.R and test/exogenous_reference.R, whose
# model files it reads back. For each case writes the posterior mean of every
# shock's contribution and the baseline (tt x (k + 1)), and the 90% bounds.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]

save_case <- function(name, file, response, type = "oir") {
  model <- read_model_from_hdf5(file.path(out, file))
  if (inherits(model, "bvecmodel")) model <- vec_to_var(model)
  hd <- historical_decomposition(model, response = response, type = type, ci = 0.9)
  write.csv(unclass(hd)[, , drop = FALSE], file.path(out, sprintf("hd_%s_mean.csv", name)), row.names = FALSE)
  write.csv(unclass(attr(hd, "lower")), file.path(out, sprintf("hd_%s_lower.csv", name)), row.names = FALSE)
  write.csv(unclass(attr(hd, "upper")), file.path(out, sprintf("hd_%s_upper.csv", name)), row.names = FALSE)
  med <- historical_decomposition(model, response = response, type = type, statistic = "median")
  write.csv(unclass(med), file.path(out, sprintf("hd_%s_median.csv", name)), row.names = FALSE)
  cat("wrote", name, nrow(hd), "periods\n")
}

save_case("var2_cons", "var2.h5", "cons")
save_case("sign_cons", "sign.h5", "cons", type = "sign")
save_case("vec_R", "vec.h5", "R")
save_case("varx_income", "varx.h5", "income")
save_case("vecx_Dp", "vecx.h5", "Dp")
