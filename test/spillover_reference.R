# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# Reference connectedness measures from bvartools' spillover(), for
# test/structural_test.cpp to hold the studio's engine to, draw by draw.
#
#   Rscript test/spillover_reference.R test/reference
#
# Run after test/structural_reference.R, whose model files it reads back. For
# each case writes every draw of the total index and of the from, to and net
# measures (keep_draws = TRUE), and the posterior mean table with its margins.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]

save_case <- function(name, file, n_ahead, type, period = NULL) {
  model <- read_model_from_hdf5(file.path(out, file))
  if (inherits(model, "bvecmodel")) model <- vec_to_var(model)
  sp <- spillover(model, n_ahead = n_ahead, type = type, keep_draws = TRUE, period = period)
  draws <- cbind(total = as.numeric(sp$total), from = unclass(sp$from), to = unclass(sp$to),
                 net = unclass(sp$net))
  write.csv(draws, file.path(out, sprintf("spillover_%s.csv", name)), row.names = FALSE)
  write.csv(sp$table, file.path(out, sprintf("spillover_%s_table.csv", name)), row.names = FALSE)
  write.csv(sp$pairwise, file.path(out, sprintf("spillover_%s_pairwise.csv", name)), row.names = FALSE)
  cat("wrote", name, "total", median(sp$total), "over", nrow(draws), "draws\n")
}

save_case("var2_gir", "var2.h5", 10, "gir")
save_case("var2_oir", "var2.h5", 4, "oir")
save_case("sign", "sign.h5", 10, "sign")
save_case("vec_gir", "vec.h5", 10, "gir")
save_case("tvp_gir_last", "tvp.h5", 10, "gir")
save_case("tvp_gir_10", "tvp.h5", 10, "gir", period = 10)
