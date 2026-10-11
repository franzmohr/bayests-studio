#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// One restriction of a sign and zero identification: the response of
/// `response` to the shock named after `impulse` at `horizon` -- infinity for
/// the long run -- is positive (+1), negative (-1) or exactly zero (0).
/// Variables are 0-based here, 1-based in the file.
struct ZeroSignRestriction
{
    int impulse = 0;
    int response = 0;
    int sign = 0;
    double horizon = 0.0;   ///< std::numeric_limits<double>::infinity() for the long run
};

struct SignZeroSpec
{
    std::vector<ZeroSignRestriction> restrictions;
    int maxTries = 1;       ///< 1 is the paper's algorithm
    bool smooth = true;     ///< Pareto smooth the importance weights
    bool oneSided = false;  ///< one-sided numerical derivative
    int period = -1;        ///< 1-based where the model moves; -1 the last
    int draws = 0;          ///< draws to resample; 0 the effective sample size
};

struct SignZeroResult
{
    int candidates = 0;     ///< posterior draws
    int tries = 0;          ///< rotations drawn in all
    int accepted = 0;       ///< draws whose rotation satisfied the signs
    int effectiveSampleSize = 0;
    double maxWeightShare = 0.0;
    double paretoK = std::numeric_limits<double>::quiet_NaN();
    int draws = 0;          ///< draws in the resample written
    QStringList warnings;   ///< bvartools' .warn_importance_sample(), where it fires
};

/// Why the model cannot be identified this way, or empty.
QString signZeroRefusal(const QString &file);

/// The restriction table checked as bvartools' .check_sign_zero_restrictions()
/// checks it: an exception names the first problem.
void checkSignZeroRestrictions(const std::vector<ZeroSignRestriction> &restrictions, int k,
                               const QStringList &variables, bool requireZero = true);

/// Arias, Rubio-Ramírez and Waggoner (2018) as bvartools'
/// add_sign_zero_restrictions() implements it: Algorithm 2 builds each
/// draw's rotation column by column on the null space of its zero
/// restrictions, Algorithm 3 weights it by the ratio of two volume elements,
/// the weights are Pareto smoothed (Vehtari et al. 2024), and the posterior is
/// resampled by them. The result is written to `output`, a copy of `file`
/// whose whole posterior is the resample -- `file` itself is not touched,
/// since a resample is not the chain it came from -- with the identification
/// in bvartools' layout: /model/sign_zero_restrictions, and
/// /model/sign_restrictions with /posterior/q/coeffs for irf(type = "sign").
SignZeroResult identifySignZero(const QString &file, const QString &output, const SignZeroSpec &spec,
                                std::uint64_t seed, const std::function<bool(int, int)> &progress = {});

/// Pareto smoothed log weights, normalised, and the fitted shape k (NaN when
/// there is no tail to fit) -- bvartools' .psis_smooth(), itself loo's.
/// -infinity marks a draw outside the sample, in and out.
std::vector<double> psisSmooth(const std::vector<double> &logWeights, double &paretoK);

/// The raw log weights of Algorithm 3 for every draw of `file` (-inf where the
/// signs failed), for any restriction set, signs alone included -- where the
/// two volume elements must cancel to a constant, which is the check of the
/// numerical one the tests make. Diagnostics, not a step of the identification.
std::vector<double> signZeroLogWeights(const QString &file, const std::vector<ZeroSignRestriction> &restrictions,
                                       std::uint64_t seed, int maxDraws = 200);

/// Internals exposed for the tests: one draw's rotation and log weight under
/// a fixed completion, from the full coefficients B' (k x m) and Sigma.
struct ArwDraw
{
    arma::mat q;            ///< empty when the signs failed
    double logWeight = -std::numeric_limits<double>::infinity();
    double logPHat = 0.0;
    int tries = 0;
};

} // namespace studio
