# Reference coefficient summaries from bvartools' summary(), for
# test/structural_test.cpp to hold the studio's summary plot to: every panel
# must carry the draws of the coefficient bvartools puts there.
#
#   Rscript test/summary_reference.R test/reference
#
# Run after test/structural_reference.R, whose model files it reads back. For
# each case writes one row per coefficient -- equation, regressor, mean,
# median and the 95% bounds -- in the order of summary()'s columns, the error
# covariance after them.

suppressPackageStartupMessages(library(bvartools))
out <- commandArgs(TRUE)[1]

flatten <- function(block, part) {
  means <- block$means
  rows <- expand.grid(equation = rownames(means), regressor = colnames(means),
                      stringsAsFactors = FALSE)
  data.frame(part = part, rows, mean = as.vector(means), median = as.vector(block$median),
             lower = as.vector(block$q_lower), upper = as.vector(block$q_upper))
}

for (name in c("var2", "vec", "svar")) {
  model <- read_model_from_hdf5(file.path(out, paste0(name, ".h5")))
  s <- summary(model, ci = 0.95)
  table <- rbind(flatten(s$a, "a"), flatten(s$sigma, "sigma"))
  write.csv(table, file.path(out, sprintf("summary_%s.csv", name)), row.names = FALSE)
  cat("wrote", name, nrow(table), "coefficients\n")
}
