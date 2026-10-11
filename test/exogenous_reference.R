# Reference models with weakly exogenous variables from bvartools, for
# test/structural_test.cpp: the builder must write the data bvartools writes
# for the same specification, and the studio's dynamic multipliers must be
# bvartools' multipliers(), draw by draw.
#
#   Rscript test/exogenous_reference.R test/reference
#
# Writes the data sets as CSV (what the builder reads), each model file, and
# its multipliers (every draw, keep_draws = TRUE) as CSV.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]
dir.create(out, showWarnings = FALSE, recursive = TRUE)
set.seed(20261013)

write_data <- function(x, name) {
  d <- data.frame(date = format(zoo::as.yearqtr(time(x))), as.data.frame(x))
  write.csv(d, file.path(out, name), row.names = FALSE)
}

save_multipliers <- function(model, name, impulse, responses, n_ahead = 8, period = NULL) {
  for (resp in responses) {
    for (type in c("permanent", "transitory")) {
      dm <- multipliers(model, impulse = impulse, response = resp, n_ahead = n_ahead,
                        type = type, keep_draws = TRUE, period = period)
      write.csv(unclass(dm), file.path(out, sprintf("multipliers_%s_%s_%s_%s.csv", name, type, impulse, resp)),
                row.names = FALSE)
    }
  }
}

holdout <- 4

# A VARX on e1: investment and income, consumption weakly exogenous; the last
# four periods held out and forecast given consumption over them.
data("e1"); e1 <- diff(log(e1)) * 100
write_data(e1, "e1.csv")
n <- nrow(e1)
train <- window(e1, end = time(e1)[n - holdout])
# bvartools wants the s periods before the horizon as well, for the lags.
future <- stats::ts(e1[(n - holdout):n, "cons", drop = FALSE], start = time(e1)[n - holdout],
                    frequency = 4)
varx <- create_bvarmodel(data = train[, c("invest", "income")], exogen = train[, "cons", drop = FALSE],
                         p = 2, s = 1, deterministic = "const", iterations = 1000, burnin = 500)
varx <- add_priors(varx, coef = list(v_i = 1, v_i_det = 1 / 10), sigma = list(df = "k", scale = 1))
varx <- add_posterior_coefficients(add_initial_values(varx))
varx <- add_forecast_input(varx, n_ahead = holdout, exogen = future)
file <- file.path(out, "varx.h5"); unlink(file); write_to_hdf5(varx, file)
save_multipliers(varx, "varx", "cons", c("invest", "income"))

# The same as a TVP-VARX with stochastic volatility, read at period 20.
tvpx <- create_bvarmodel(data = train[, c("invest", "income")], exogen = train[, "cons", drop = FALSE],
                         p = 1, s = 1, deterministic = "const", tvp = TRUE, error = "sv",
                         iterations = 600, burnin = 300)
tvpx <- add_priors(tvpx, coef = list(v_i = 1, v_i_det = 1 / 10, shape = 3, rate = 0.0001, rate_det = 0.01),
                   sigma = list(mu = 0, v_i = 1, shape = 3, rate = 0.03, state_variance = .05, offset = 10^-4))
tvpx <- add_posterior_coefficients(add_initial_values(tvpx))
file <- file.path(out, "tvpx.h5"); unlink(file); write_to_hdf5(tvpx, file)
save_multipliers(tvpx, "tvpx", "cons", "income", period = 20)

# A VECX on e6 with a synthetic exogenous variable, as in bvartools' example.
data("e6"); e6 <- e6 * 100
g <- stats::ts(as.numeric(e6[, "R"]) * 0.3 + cumsum(rnorm(nrow(e6), sd = 0.3)),
               start = start(e6), frequency = frequency(e6))
e6x <- cbind(e6, g = g)
colnames(e6x) <- c("R", "Dp", "g")
write_data(e6x, "e6x.csv")
n <- nrow(e6x)
train <- window(e6x, end = time(e6x)[n - holdout])
future <- stats::ts(e6x[(n - holdout - 1):n, "g", drop = FALSE], start = time(e6x)[n - holdout - 1],
                    frequency = 4)
vecx <- create_bvecmodel(data = train[, c("R", "Dp")], exogen = train[, "g", drop = FALSE],
                         p = 2, s = 2, r = 1, const = "unrestricted", iterations = 1000, burnin = 500)
vecx <- add_priors(vecx, coef = list(v_i = 1, v_i_det = 1 / 10), coint = list(v_i = .1, p_tau_i = 1),
                   sigma = list(df = "k", scale = 1))
vecx <- add_posterior_coefficients(add_initial_values(vecx))
vecx <- add_forecast_input(vecx, n_ahead = holdout, exogen = future)
file <- file.path(out, "vecx.h5"); unlink(file); write_to_hdf5(vecx, file)
save_multipliers(vecx, "vecx", "g", c("R", "Dp"))
cat("wrote varx, tvpx, vecx\n")
