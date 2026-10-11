#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// The posterior draws of a VAR or VEC model file cut into the blocks of
/// bvartools' summary plot, plot.bvarmodel() and plot.bvecmodel(): one grid
/// per block, a row per equation and a column per regressor, so that panel
/// (i, j) carries the draws of the coefficient of regressor j in equation i.
///
///   VAR   lagged endogenous variables, exogenous variables, deterministic
///         terms, contemporaneous endogenous variables (structural), the
///         error covariance
///   VEC   the cointegration matrix Pi = alpha beta', lagged differences,
///         differenced exogenous variables, unrestricted deterministic terms,
///         contemporaneous endogenous variables, the error covariance
///
/// A model whose coefficients move gives a path per panel instead of draws,
/// and so does the covariance of a model whose covariance moves.
struct CoefficientPanel
{
    enum class Kind
    {
        Draws,      ///< `draws`, one per kept draw
        Path,       ///< `path`: periods x 3, the lower bound, median and upper bound
        Fixed,      ///< `value`, which every draw shares: A_0's ones and zeros
    };

    Kind kind = Kind::Draws;
    arma::vec draws;
    arma::mat path;
    double value = 0.0;
};

struct CoefficientBlock
{
    QString title;
    QStringList columns;                   ///< regressor names
    /// k x columns panels, column by column: panel (i, j) is panels[j * k + i].
    std::vector<CoefficientPanel> panels;
};

struct CoefficientSummary
{
    QString title;                         ///< "Bayesian TVP-SV-VAR model with p = 2"
    QStringList equations;                 ///< row names
    std::vector<CoefficientBlock> blocks;
    double start = 1.0;                    ///< the time of period 1, R's tsp
    double frequency = 1.0;
    int draws = 0;
};

/// The summary of `file`, paths summarised by the central `ci` interval
/// (bvartools' default 0.95). Throws with the reason where the model is not a
/// VAR or VEC with draws.
CoefficientSummary coefficientSummary(const QString &file, double ci = 0.95);

} // namespace studio
