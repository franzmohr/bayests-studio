#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// Impulse responses and forecast error variance decompositions from the
/// posterior draws in a model file.
///
/// The definitions are bvartools' (irf(), fevd(), src/ir.cpp and
/// R/collect_draws.R), so the two give the same numbers from the same draws:
///
///   feir   Phi_h                         a shock to the reduced form error
///   oir    Phi_h chol(Sigma)'            one standard deviation, recursive
///   gir    Phi_h Sigma e_j / sqrt(s_jj)  Pesaran and Shin (1998)
///   sir    Phi_h A_0^-1                  a structural model's shocks
///   sgir   Phi_h A_0^-1 Sigma e_j / sqrt(s_jj)
///
/// A structural model stores A_0 as the last k(k-1)/2 coefficients and the
/// covariance of the structural errors; the recursion takes the reduced form
/// A_0^-1 A_i, which is why feir, oir and gir are refused there and sir and
/// sgir everywhere else -- as bvartools refuses them. A VEC is rewritten as the
/// level VAR its draws imply first. A model whose coefficients or covariance
/// move is read at one period, the last by default.
enum class ResponseType
{
    Feir,
    Oir,
    Gir,
    Sir,
    Sgir,
};

QString responseTypeName(ResponseType type);

struct ResponseRequest
{
    ResponseType type = ResponseType::Oir;
    int horizon = 20;
    int period = -1;        ///< 1-based estimation period; -1 is the last
    bool cumulative = false;
    /// For feir and sir: a shock of one standard deviation of the error
    /// rather than of one unit. oir, gir and sgir are in standard deviations
    /// already.
    bool standardDeviation = false;
    int maxDraws = 4000;    ///< thinned evenly to at most this many
};

/// What can be done with the model at all, decided from the file.
struct StructuralSupport
{
    bool ok = false;
    QString reason;          ///< why not, when !ok
    bool structural = false; ///< then sir/sgir, else feir/oir/gir
    bool moving = false;     ///< coefficients or covariance per period
    int periods = 0;
    int k = 0;
    QStringList variables;
};

StructuralSupport structuralSupport(const QString &file);

/// The names of the endogenous variables in levels: /data/train/y's
/// `variables`, less the "d." bvartools puts in front of a VEC's differences.
QStringList levelNames(const QString &file);

/// Responses of every variable to a shock in `impulse`: draws[d] is
/// (horizon + 1) x k, rows horizons and columns responses.
std::vector<arma::mat> impulseResponses(const QString &file, const ResponseRequest &request, int impulse);

/// Forecast error variance decomposition of `response`, oir or sir only:
/// draws[d] is (horizon + 1) x k, the share of each shock in the h-step
/// forecast error variance, each row summing to one.
std::vector<arma::mat> varianceDecomposition(const QString &file, const ResponseRequest &request, int response);

} // namespace studio
