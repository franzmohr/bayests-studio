# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# Reference impulse responses and variance decompositions from bvartools, for
# test/structural_test.cpp to hold the studio's engine to, draw by draw.
#
#   Rscript test/structural_reference.R test/reference
#
# Writes one model file per case -- estimated by bvartools itself and written
# with write_to_hdf5(), so the draws are the ones bvartools computes from --
# and beside each the responses (every draw, keep_draws = TRUE) and the
# posterior mean variance decomposition as CSV. Rerun it when bvartools'
# definitions change; the test then says whether the studio still agrees.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]
dir.create(out, showWarnings = FALSE, recursive = TRUE)
set.seed(20261011)

data("e1"); e1 <- diff(log(e1)) * 100; e1 <- window(e1, end = c(1978, 4))
data("e6"); e6 <- e6 * 100
n_ahead <- 8

estimate <- function(model) {
  model <- add_initial_values(model)
  add_posterior_coefficients(model)
}

save_case <- function(name, model, cases, fevd_types) {
  file <- file.path(out, paste0(name, ".h5"))
  unlink(file)
  write_to_hdf5(model, file)
  var_model <- if (inherits(model, "bvecmodel")) vec_to_var(model) else model
  for (case in cases) {
    r <- irf(var_model, impulse = case$impulse, response = case$response, n_ahead = n_ahead,
             type = case$type, keep_draws = TRUE)
    write.csv(unclass(r), file.path(out, sprintf("%s_irf_%s_%s_%s.csv", name, case$type,
                                                 case$impulse, case$response)), row.names = FALSE)
  }
  for (type in fevd_types) {
    f <- fevd(var_model, response = cases[[1]]$response, n_ahead = n_ahead, type = type)
    write.csv(unclass(f), file.path(out, sprintf("%s_fevd_%s_%s.csv", name, type, cases[[1]]$response)),
              row.names = FALSE)
  }
  cat("wrote", name, "\n")
}

# The article's VAR(2), Wishart errors.
var2 <- create_bvarmodel(e1, p = 2, deterministic = "const", iterations = 1000, burnin = 500)
var2 <- add_priors(var2, coef = list(v_i = 1, v_i_det = 1/10), sigma = list(df = "k", scale = 1))
var2 <- estimate(var2)
save_case("var2", var2,
          list(list(type = "feir", impulse = "income", response = "cons"),
               list(type = "oir", impulse = "income", response = "cons"),
               list(type = "gir", impulse = "invest", response = "income")),
          "oir")

# The same VAR identified by sign restrictions: an income shock raises income
# and consumption on impact, a consumption shock raises consumption.
sign <- add_sign_restrictions(var2, data.frame(impulse = c("income", "income", "cons"),
                                               response = c("income", "cons", "cons"),
                                               sign = c(1, 1, 1), horizon = c(0, 0, 0)),
                              max_tries = 500)
save_case("sign", sign,
          list(list(type = "sign", impulse = "income", response = "cons"),
               list(type = "sign", impulse = "cons", response = "invest")),
          "sign")

# A structural VAR with gamma errors.
svar <- create_bvarmodel(e1, p = 2, deterministic = "const", structural = TRUE, error = "gamma",
                         iterations = 1000, burnin = 500)
svar <- add_priors(svar, coef = list(v_i = 1, v_i_det = 1/10), sigma = list(shape = 3, rate = 0.01))
svar <- estimate(svar)
save_case("svar", svar,
          list(list(type = "sir", impulse = "invest", response = "cons"),
               list(type = "sgir", impulse = "income", response = "invest")),
          "sir")

# The article's VEC on e6.
vec <- create_bvecmodel(e6, p = 4, r = 1, const = "unrestricted", iterations = 1000, burnin = 500)
vec <- add_priors(vec, coef = list(v_i = 1, v_i_det = 1/10),
                  coint = list(v_i = .1, p_tau_i = 1), sigma = list(df = "k", scale = 1))
vec <- estimate(vec)
save_case("vec", vec, list(list(type = "oir", impulse = "R", response = "Dp")), "oir")

# A TVP-VAR with stochastic volatility, read at the last period.
tvp <- create_bvarmodel(e1, p = 1, deterministic = "const", tvp = TRUE, error = "sv",
                        iterations = 1000, burnin = 500)
tvp <- add_priors(tvp, coef = list(v_i = 1, v_i_det = 1/10, shape = 3, rate = 0.0001, rate_det = 0.01),
                  sigma = list(mu = 0, v_i = 1, shape = 3, rate = 0.03, state_variance = .05, offset = 10^-4))
tvp <- estimate(tvp)
save_case("tvp", tvp, list(list(type = "oir", impulse = "income", response = "cons")), "oir")
