# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# Reference inclusion probabilities from bvartools' summary(), for
# test/structural_test.cpp to hold the studio's to: one per coefficient, in
# the order of summary()'s columns, and those of the covariance block.
#
#   Rscript test/inclusion_reference.R test/reference
#
# Estimates its own models -- variable selection is not among the cases of
# test/structural_reference.R -- and writes each beside its probabilities.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]
set.seed(20261014)
data("e1"); e1 <- diff(log(e1)) * 100
data("e6"); e6 <- e6 * 100

save_case <- function(name, model) {
  file <- file.path(out, paste0(name, ".h5"))
  unlink(file)
  write_to_hdf5(model, file)
  s <- summary(model)
  write.csv(data.frame(p = as.vector(s$a$lambda)), file.path(out, sprintf("inclusion_%s_a.csv", name)),
            row.names = FALSE)
  if (!is.null(s$sigma$lambda)) {
    write.csv(data.frame(p = as.vector(s$sigma$lambda)), file.path(out, sprintf("inclusion_%s_psi.csv", name)),
              row.names = FALSE)
  }
  cat("wrote", name, length(s$a$lambda), "\n")
}

estimate <- function(model, ...) {
  model <- add_priors(model, ...)
  add_posterior_coefficients(add_initial_values(model))
}

# SSVS on the article's VAR(2), semiautomatic.
ssvs <- create_bvarmodel(e1, p = 2, deterministic = "const", varsel = "ssvs", iterations = 1000, burnin = 500)
ssvs <- estimate(ssvs, coef = list(v_i = 1, v_i_det = 1 / 10), sigma = list(df = "k", scale = 1),
                 varsel = list(inprior = .5, semiautomatic = c(.1, 10)))
save_case("ssvs", ssvs)

# BVS with a covariance block whose elements are selected too, and the
# deterministic terms left out of the selection.
bvs <- create_bvarmodel(e1, p = 2, deterministic = "const", error = "gamma+covar", varsel = "bvs",
                        iterations = 1000, burnin = 500)
bvs <- estimate(bvs, coef = list(v_i = 1 / 9, v_i_det = 1 / 10), sigma = list(shape = 3, rate = 0.01),
                psi = list(v_i = 1 / 9), varsel = list(inprior = .5, exclude_det = TRUE, covar = TRUE))
save_case("bvs_covar", bvs)

# BVS on a structural VAR: the contemporaneous coefficients are selected too.
sbvs <- create_bvarmodel(e1, p = 1, deterministic = "const", structural = TRUE, error = "gamma",
                         varsel = "bvs", iterations = 1000, burnin = 500)
sbvs <- estimate(sbvs, coef = list(v_i = 1 / 9, v_i_det = 1 / 10), sigma = list(shape = 3, rate = 0.01),
                 varsel = list(inprior = .5))
save_case("bvs_structural", sbvs)

# BVS on a VEC: the loadings are never selected.
vbvs <- create_bvecmodel(e6, p = 3, r = 1, const = "unrestricted", varsel = "bvs", iterations = 1000,
                         burnin = 500)
vbvs <- estimate(vbvs, coef = list(v_i = 1 / 9, v_i_det = 1 / 10), coint = list(v_i = .1, p_tau_i = 1),
                 sigma = list(df = "k", scale = 1), varsel = list(inprior = .5))
save_case("bvs_vec", vbvs)
