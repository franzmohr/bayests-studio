# Reference results for the studio's sign and zero restrictions, from bvartools.
#
#   Rscript test/signzero_reference.R test/reference
#
# Run after test/structural_reference.R, whose var2.h5 it identifies. Writes
#   psis.csv            log weights, bvartools' Pareto smoothed log weights
#   psis_k.csv          and the fitted shape
#   signzero.h5         var2 identified by add_sign_zero_restrictions()
#   signzero_*.csv      its responses (every resampled draw) and summary
# The studio is held to the smoother exactly, to responses computed from
# bvartools' rotations exactly, and to bvartools' own identification within
# Monte Carlo error.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]
set.seed(20261012)

# The smoother, on weights with a heavy upper tail.
lw <- c(rnorm(400), 3 * rexp(100))
smoothed <- bvartools:::.psis_smooth(lw)
write.csv(data.frame(log_weight = lw, smoothed = smoothed$log_weights),
          file.path(out, "psis.csv"), row.names = FALSE)
write.csv(data.frame(pareto_k = smoothed$pareto_k), file.path(out, "psis_k.csv"), row.names = FALSE)

# The identification: an investment shock raises investment and leaves income
# unchanged on impact; an income shock raises income and consumption.
var2 <- read_model_from_hdf5(file.path(out, "var2.h5"))
restrictions <- data.frame(impulse = c("invest", "invest", "income", "income"),
                           response = c("invest", "income", "income", "cons"),
                           sign = c(1, 0, 1, 1), horizon = c(0, 0, 0, 0))
identified <- add_sign_zero_restrictions(var2, restrictions, draws = 1000)
file <- file.path(out, "signzero.h5")
unlink(file)
# bvartools builds before 7d85e21 (2026-10-11) stop writing a sign and zero
# identification on the max_tries its sign_restrictions element lacks; giving
# it one is harmless with later builds too.
if (is.null(identified$model$sign_restrictions$max_tries)) {
  identified$model$sign_restrictions$max_tries <- identified$model$sign_zero_restrictions$max_tries
}
write_to_hdf5(identified, file)
for (imp in c("invest", "income")) {
  for (resp in c("income", "cons")) {
    r <- irf(identified, impulse = imp, response = resp, n_ahead = 8, type = "sign", keep_draws = TRUE)
    write.csv(unclass(r), file.path(out, sprintf("signzero_irf_sign_%s_%s.csv", imp, resp)), row.names = FALSE)
  }
}
f <- fevd(identified, response = "cons", n_ahead = 8, type = "sign")
write.csv(unclass(f), file.path(out, "signzero_fevd_sign_cons.csv"), row.names = FALSE)
s <- identified$model$sign_zero_restrictions
write.csv(data.frame(accepted = s$accepted, effective = s$effective_sample_size,
                     pareto_k = s$pareto_k, max_weight_share = s$max_weight_share),
          file.path(out, "signzero_summary.csv"), row.names = FALSE)
cat("accepted", s$accepted, "effective", s$effective_sample_size, "pareto k", s$pareto_k, "\n")
