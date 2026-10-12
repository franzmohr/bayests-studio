// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>
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
    Sign,   ///< chol(Sigma)' Q, Q the rotation /posterior/q/coeffs holds for the draw
};

/// One sign restriction: the response of `response` to the shock of
/// `impulse` at `horizon` has the sign of `sign` (+1 or -1), and exceeds
/// `threshold` in size -- 0 for a sign alone, bvartools' rule; Pinchetti (2026)
/// asks for 0.01. Variables are 0-based here and 1-based in the file, as
/// bvartools writes them.
struct SignRestriction
{
    int impulse = 0;
    int response = 0;
    int sign = 1;
    int horizon = 0;
    double threshold = 0.0;
};

/// A sign restricted identification as bvartools' add_sign_restrictions()
/// stores it: the table under /model/sign_restrictions, the rotations under
/// /posterior/q/coeffs.
///
/// `rotated` limits the search to the first n shocks: the rotation is the
/// identity outside its leading n x n block, so the other shocks stay those of
/// the Cholesky factor -- recursive, in the variables' order. That is the set
/// Jarociński and Karadi (2020) and Pinchetti (2026) search over, with their
/// surprises ordered first; 0 rotates every shock, bvartools' rule. The
/// thresholds and `rotated` are stored beside bvartools' table, as attributes
/// it does not read, so R reads the rotations back unchanged.
struct SignIdentification
{
    std::vector<SignRestriction> restrictions;
    int rotated = 0;
    int maxTries = 1000;
    int period = -1;          ///< 1-based, -1 for the last; meaningful where Sigma moves
    int accepted = 0;         ///< draws a rotation was found for
    int draws = 0;
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

    /// Whether the model can be identified by sign restrictions at all, and
    /// the identification it already carries.
    bool signAllowed = false;
    QString signRefusal;
    std::optional<SignIdentification> sign;
};

StructuralSupport structuralSupport(const QString &file);

/// The names of the endogenous variables in levels: /data/train/y's
/// `variables`, less the "d." bvartools puts in front of a VEC's differences.
QStringList levelNames(const QString &file);

/// The identification stored in the file, if any.
std::optional<SignIdentification> readSignIdentification(const QString &file);

/// Searches a rotation for every draw that satisfies `restrictions` --
/// bvartools' draw_sign_restricted_q(): Q from the QR decomposition of a
/// standard normal matrix, the signs of R's diagonal moved into Q so the draw
/// is uniform on the orthogonal group, and a restricted shock that fails
/// tried again with its column negated -- and writes the result in bvartools'
/// layout, replacing an earlier identification. Draws without an admissible
/// rotation within `maxTries` are NaN, as there. `progress(done, total)`
/// returning false cancels and writes nothing. Returns the identification
/// written.
SignIdentification identifySigns(const QString &file, const SignIdentification &spec, std::uint64_t seed,
                                 const std::function<bool(int, int)> &progress = {});

/// Responses of every variable to a shock in `impulse`: draws[d] is
/// (horizon + 1) x k, rows horizons and columns responses.
std::vector<arma::mat> impulseResponses(const QString &file, const ResponseRequest &request, int impulse);

/// Forecast error variance decomposition of `response`, oir or sir only:
/// draws[d] is (horizon + 1) x k, the share of each shock in the h-step
/// forecast error variance, each row summing to one.
std::vector<arma::mat> varianceDecomposition(const QString &file, const ResponseRequest &request, int response);

} // namespace studio
