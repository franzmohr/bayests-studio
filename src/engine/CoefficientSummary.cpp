// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/CoefficientSummary.h"

#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"
#include "engine/Structural.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace studio
{

namespace
{

/// An integer attribute of /model, scalar or a one-element array (bvartools
/// writes the latter), or `fallback` where it is missing.
int modelInt(const HighFive::Group &model, const char *name, int fallback)
{
    if (!model.hasAttribute(name))
    {
        return fallback;
    }
    const HighFive::Attribute a = model.getAttribute(name);
    if (a.getSpace().getElementCount() == 0)
    {
        return fallback;
    }
    if (a.getSpace().getNumberDimensions() == 0)
    {
        return static_cast<int>(a.read<double>());
    }
    const auto v = a.read<std::vector<double>>();
    return v.empty() ? fallback : static_cast<int>(v.front());
}

/// R's quantile(type = 7) of a sorted sample.
double quantile7(const std::vector<double> &v, double q)
{
    const double h = (static_cast<double>(v.size()) - 1.0) * q;
    const auto lo = static_cast<std::size_t>(std::floor(h));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (h - static_cast<double>(lo)) * (v[hi] - v[lo]);
}

/// The regressor part of a builder name "eq ← regressor".
QString regressorOf(const QString &coefficient)
{
    const qsizetype at = coefficient.indexOf(QStringLiteral(" ← "));
    return at < 0 ? coefficient : coefficient.mid(at + 3);
}

/// `count` names starting at regressor `first`: from bvartools' x
/// `variables`, else from the builder's coefficient names (one per equation
/// and regressor, regressor by regressor, after `offset` coefficients), else
/// empty.
QStringList regressorNames(const QStringList &xVariables, const QStringList &coefficients, int k, int offset,
                           int nx, int first, int count)
{
    QStringList out;
    if (xVariables.size() == nx)
    {
        out = xVariables.mid(first, count);
    }
    else if (coefficients.size() >= offset + k * nx)
    {
        for (int q = first; q < first + count; ++q)
        {
            out << regressorOf(coefficients[offset + q * k]);
        }
    }
    return out.size() == count ? out : QStringList();
}

/// Every draw's value of one quantity at one period.
class Draws
{
public:
    Draws(const Matrix &m, std::size_t perPeriod, bool moving) : m_(m), perPeriod_(perPeriod), moving_(moving) {}
    double at(std::size_t pos, int period, std::size_t d) const
    {
        return m_.at((moving_ ? static_cast<std::size_t>(period) * perPeriod_ : 0) + pos, d);
    }
    std::size_t draws() const { return m_.cols; }

private:
    const Matrix &m_;
    std::size_t perPeriod_;
    bool moving_;
};

/// The level names of the endogenous variables, padded to k.
QStringList levelNamesOf(const QString &file, int k)
{
    QStringList names = levelNames(file);
    if (names.size() > k)
    {
        names = names.mid(0, k);
    }
    for (int i = static_cast<int>(names.size()); i < k; ++i)
    {
        names << QStringLiteral("y%1").arg(i + 1);
    }
    return names;
}

} // namespace

CoefficientSummary coefficientSummary(const QString &file, double ci)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const CheckReport report = ModelRunner::check(file, QString());
    if (!report.ok)
    {
        throw std::runtime_error(report.error.toStdString());
    }
    if (!report.hasPosterior)
    {
        throw std::runtime_error(QObject::tr("The model has no posterior draws yet.").toStdString());
    }
    const QString algorithm = report.algorithm;
    const bool vec = algorithm.startsWith(QLatin1String("Vec"));
    if (!(vec || algorithm.startsWith(QLatin1String("Var"))) || algorithm.endsWith(QLatin1String("Discount")))
    {
        throw std::runtime_error(
            QObject::tr("The summary plot is drawn for the VAR and VEC samplers; %1 is not one of them.")
                .arg(algorithm)
                .toStdString());
    }

    int m = 0, s = 0, n = 0;
    bool grid = false;
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::Group model = h5.getGroup("/model");
        m = modelInt(model, "m", 0);
        s = modelInt(model, "s", 0);
        n = modelInt(model, "n", 0);
        grid = h5.exist("/model/quantiles");
    }
    if (grid)
    {
        // bvartools' .refuse_quantile_grid(): the draws of every level are
        // stacked, and one grid of panels cannot show them.
        throw std::runtime_error(
            QObject::tr("A grid of quantiles stacks the draws of every level; the summary plot shows one.")
                .toStdString());
    }

    const int k = report.k;
    const int p = report.p;
    const int rank = report.rank;
    const int kBeta = report.kBeta;
    const int tt = std::max(1, report.periods);
    const bool structural = report.structural;
    const int nStruct = structural ? k * (k - 1) / 2 : 0;
    const bool tvp = algorithm.contains(QLatin1String("Tvp"));
    const bool sv = algorithm.contains(QLatin1String("Stochvol"));
    const bool ald = algorithm.endsWith(QLatin1String("Ald"));

    const Matrix a = readMatrix(file, QStringLiteral("/posterior/a/coeffs"));
    const std::size_t nparams = tvp ? a.rows / static_cast<std::size_t>(tt) : a.rows;
    const int periods = tvp ? tt : 1;
    const Draws coef(a, nparams, tvp);

    // Variable selection: the indicator draws, one per coefficient of a
    // period, and the positions selection reached (one-based in the file).
    bool selecting = false;
    QString selectionName;
    double priorInclusion = std::numeric_limits<double>::quiet_NaN();
    arma::vec inclusion;
    std::vector<bool> selectedAt(nparams, false);
    {
        bool haveLambda = false;
        {
            const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
            haveLambda = h5.exist("/posterior/a/lambda");
        }
        if (haveLambda)
        {
            const Matrix lambda = readMatrix(file, QStringLiteral("/posterior/a/lambda"));
            if (lambda.rows == nparams && lambda.cols > 0)
            {
                selecting = true;
                inclusion.zeros(nparams);
                for (std::size_t q = 0; q < nparams; ++q)
                {
                    for (std::size_t d = 0; d < lambda.cols; ++d)
                    {
                        inclusion(q) += lambda.at(q, d);
                    }
                    inclusion(q) /= static_cast<double>(lambda.cols);
                }
                const auto optional = [&](const char *path) {
                    try
                    {
                        return readMatrix(file, QString::fromLatin1(path));
                    }
                    catch (const std::exception &)
                    {
                        return Matrix();
                    }
                };
                const Matrix include = optional("/priors/a/include");
                for (const double v : include.values)
                {
                    const auto q = static_cast<std::size_t>(v) - 1;
                    if (v >= 1 && q < nparams)
                    {
                        selectedAt[q] = true;
                    }
                }
                const Matrix inprior = optional("/priors/a/inprior");
                if (!inprior.values.empty())
                {
                    priorInclusion = inprior.values.front();
                }
                try
                {
                    const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
                    const HighFive::Attribute attr = h5.getGroup("/model").getAttribute("varsel");
                    selectionName = QString::fromStdString(attr.getSpace().getNumberDimensions() == 0
                                                               ? attr.read<std::string>()
                                                               : attr.read<std::vector<std::string>>().front());
                }
                catch (const std::exception &)
                {
                    selectionName = QStringLiteral("bvs");
                }
            }
        }
    }
    // The inclusion of coefficient `pos`, on panel `pn`.
    const auto mark = [&](CoefficientPanel &pn, std::size_t pos) {
        if (selecting)
        {
            pn.inclusion = inclusion(pos);
            pn.selected = selectedAt[pos];
        }
    };
    const std::size_t nd = a.cols;

    const QStringList xVariables =
        readStringsAttribute(file, QStringLiteral("/data/train/x"), QStringLiteral("variables"));
    const QStringList coefficients =
        readStringsAttribute(file, QStringLiteral("/data/train/z"), QStringLiteral("coefficients"));

    CoefficientSummary out;
    out.selection = selectionName;
    out.priorInclusion = priorInclusion;
    out.equations = levelNamesOf(file, k);
    out.draws = static_cast<int>(nd);
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::DataSet y = h5.getDataSet("/data/train/y");
        if (y.hasAttribute("tsp"))
        {
            const auto tsp = y.getAttribute("tsp").read<std::vector<double>>();
            if (tsp.size() == 3 && tsp[2] > 0)
            {
                out.start = tsp[0];
                out.frequency = tsp[2];
            }
        }
    }

    // The title bvartools prints above every figure.
    QString title = QStringLiteral("Bayesian ");
    if (tvp)
    {
        title += QStringLiteral("TVP-");
    }
    if (sv)
    {
        title += QStringLiteral("SV-");
    }
    if (ald)
    {
        title += QStringLiteral("Quantile-");
    }
    if (structural)
    {
        title += QStringLiteral("S");
    }
    QStringList spec{QStringLiteral("p = %1").arg(p)};
    if (m > 0)
    {
        spec << QStringLiteral("s = %1").arg(s);
    }
    if (vec)
    {
        title += QStringLiteral("VEC model with ");
        spec << QStringLiteral("r = %1").arg(rank);
        title += spec.join(QStringLiteral(", "));
    }
    else
    {
        title += QStringLiteral("VAR model with ") + spec.join(QStringLiteral(" and "));
    }
    out.title = title;

    const double lowP = (1.0 - ci) / 2.0;
    const double highP = 1.0 - lowP;
    // One panel from a function of (period, draw).
    const auto panel = [&](int nPeriods, const auto &value) {
        CoefficientPanel pn;
        if (nPeriods > 1)
        {
            pn.kind = CoefficientPanel::Kind::Path;
            pn.path.set_size(static_cast<arma::uword>(nPeriods), 3);
            std::vector<double> v(nd);
            for (int t = 0; t < nPeriods; ++t)
            {
                for (std::size_t d = 0; d < nd; ++d)
                {
                    v[d] = value(t, d);
                }
                std::sort(v.begin(), v.end());
                const auto tu = static_cast<arma::uword>(t);
                pn.path(tu, 0) = quantile7(v, lowP);
                pn.path(tu, 1) = quantile7(v, 0.5);
                pn.path(tu, 2) = quantile7(v, highP);
            }
            return pn;
        }
        pn.draws.set_size(nd);
        for (std::size_t d = 0; d < nd; ++d)
        {
            pn.draws(d) = value(0, d);
        }
        if (nd > 0 && pn.draws.min() == pn.draws.max())
        {
            pn.kind = CoefficientPanel::Kind::Fixed;
            pn.value = pn.draws(0);
        }
        return pn;
    };
    const auto ordinaryBlock = [&](const QString &blockTitle, QStringList names, std::size_t firstCoefficient,
                                   int count, const QString &fallbackStem) {
        CoefficientBlock b;
        b.title = blockTitle;
        if (names.size() != count)
        {
            names.clear();
            for (int j = 0; j < count; ++j)
            {
                names << QStringLiteral("%1%2").arg(fallbackStem).arg(j + 1);
            }
        }
        b.columns = names;
        for (int j = 0; j < count; ++j)
        {
            for (int i = 0; i < k; ++i)
            {
                const std::size_t pos = firstCoefficient + static_cast<std::size_t>(j * k + i);
                b.panels.push_back(panel(periods, [&](int t, std::size_t d) { return coef.at(pos, t, d); }));
                mark(b.panels.back(), pos);
            }
        }
        out.blocks.push_back(std::move(b));
    };

    std::size_t offset = 0;   // coefficients of the blocks before
    if (!vec)
    {
        const int nx = k * p + m * (s + 1) + n;
        if (static_cast<std::size_t>(k * nx + nStruct) != nparams)
        {
            throw std::runtime_error(QObject::tr("The coefficients of this file do not have the layout its model "
                                                 "attributes describe.").toStdString());
        }
        if (p > 0)
        {
            QStringList names = regressorNames(xVariables, coefficients, k, 0, nx, 0, k * p);
            if (names.isEmpty())
            {
                for (int l = 1; l <= p; ++l)
                {
                    for (const QString &v : out.equations)
                    {
                        names << QStringLiteral("%1.l%2").arg(v).arg(l);
                    }
                }
            }
            ordinaryBlock(QObject::tr("Lagged endogenous variables"), names, 0, k * p, QStringLiteral("y"));
            offset += static_cast<std::size_t>(k * k * p);
        }
        if (m > 0)
        {
            ordinaryBlock(QObject::tr("Exogenous variables"),
                          regressorNames(xVariables, coefficients, k, 0, nx, k * p, m * (s + 1)), offset,
                          m * (s + 1), QStringLiteral("x"));
            offset += static_cast<std::size_t>(k * m * (s + 1));
        }
        if (n > 0)
        {
            ordinaryBlock(QObject::tr("Deterministic terms"),
                          regressorNames(xVariables, coefficients, k, 0, nx, k * p + m * (s + 1), n), offset, n,
                          QStringLiteral("det."));
            offset += static_cast<std::size_t>(k * n);
        }
    }
    else
    {
        const std::size_t nAlpha = static_cast<std::size_t>(k * rank);
        const std::size_t rest = nparams - nAlpha - static_cast<std::size_t>(nStruct);
        const int nx = static_cast<int>(rest / static_cast<std::size_t>(k));
        const int nGamma = k * std::max(p - 1, 0);
        const int nUpsilon = m * s;
        if (rest % static_cast<std::size_t>(k) != 0 || nx < nGamma + nUpsilon)
        {
            throw std::runtime_error(QObject::tr("The coefficients of this file do not have the layout its model "
                                                 "attributes describe.").toStdString());
        }
        if (rank > 0)
        {
            // Pi = alpha beta', k x k_beta, per draw and period: the columns
            // are those of w, not of alpha.
            const Matrix beta = readMatrix(file, QStringLiteral("/posterior/beta/coeffs"));
            const std::size_t nBeta = static_cast<std::size_t>(kBeta * rank);
            const bool betaMoves = tvp && beta.rows == nBeta * static_cast<std::size_t>(tt);
            const Draws betaDraws(beta, nBeta, betaMoves);
            QStringList names = readStringsAttribute(file, QStringLiteral("/data/train/w"), QStringLiteral("variables"));
            if (names.size() != kBeta)
            {
                names = readStringsAttribute(file, QStringLiteral("/data/train/w"), QStringLiteral("terms"));
            }
            if (names.size() != kBeta)
            {
                names.clear();
                for (int j = 0; j < kBeta; ++j)
                {
                    names << (j < k ? QStringLiteral("l.%1").arg(out.equations.value(j))
                                    : QStringLiteral("l.d%1").arg(j - k + 1));
                }
            }
            CoefficientBlock b;
            b.title = QObject::tr("Cointegration matrix");
            b.columns = names;
            for (int j = 0; j < kBeta; ++j)
            {
                for (int i = 0; i < k; ++i)
                {
                    b.panels.push_back(panel(periods, [&](int t, std::size_t d) {
                        double v = 0.0;
                        for (int r = 0; r < rank; ++r)
                        {
                            v += coef.at(static_cast<std::size_t>(r * k + i), t, d) *
                                 betaDraws.at(static_cast<std::size_t>(r * kBeta + j), t, d);
                        }
                        return v;
                    }));
                    // The loadings are never selected over: the relation is
                    // always in the model.
                    if (selecting)
                    {
                        b.panels.back().inclusion = 1.0;
                    }
                }
            }
            out.blocks.push_back(std::move(b));
        }
        offset = nAlpha;
        const int offsetNames = static_cast<int>(nAlpha);
        if (nGamma > 0)
        {
            ordinaryBlock(QObject::tr("Lagged differenced endogenous variables"),
                          regressorNames(xVariables, coefficients, k, offsetNames, nx, 0, nGamma), offset, nGamma,
                          QStringLiteral("d.y"));
            offset += static_cast<std::size_t>(k * nGamma);
        }
        if (nUpsilon > 0)
        {
            ordinaryBlock(QObject::tr("Differenced exogenous variables"),
                          regressorNames(xVariables, coefficients, k, offsetNames, nx, nGamma, nUpsilon), offset,
                          nUpsilon, QStringLiteral("d.x"));
            offset += static_cast<std::size_t>(k * nUpsilon);
        }
        const int nC = nx - nGamma - nUpsilon;
        if (nC > 0)
        {
            ordinaryBlock(QObject::tr("Unrestricted deterministic terms"),
                          regressorNames(xVariables, coefficients, k, offsetNames, nx, nGamma + nUpsilon, nC), offset,
                          nC, QStringLiteral("det."));
            offset += static_cast<std::size_t>(k * nC);
        }
    }

    if (structural)
    {
        // A_0: unit lower triangular, its free elements stored column by
        // column after every other coefficient.
        CoefficientBlock b;
        b.title = QObject::tr("Contemporaneous endogenous variables");
        b.columns = out.equations;
        const std::size_t first = nparams - static_cast<std::size_t>(nStruct);
        for (int j = 0; j < k; ++j)
        {
            for (int i = 0; i < k; ++i)
            {
                if (i > j)
                {
                    // Free elements before column j, then the rows below j.
                    const std::size_t idx =
                        static_cast<std::size_t>(j * k - j * (j + 1) / 2 + (i - j - 1));
                    const std::size_t pos = first + idx;
                    b.panels.push_back(panel(periods, [&](int t, std::size_t d) { return coef.at(pos, t, d); }));
                    mark(b.panels.back(), pos);
                }
                else
                {
                    CoefficientPanel fixed;
                    fixed.kind = CoefficientPanel::Kind::Fixed;
                    fixed.value = i == j ? 1.0 : 0.0;
                    if (selecting)
                    {
                        fixed.inclusion = fixed.value;
                    }
                    b.panels.push_back(fixed);
                }
            }
        }
        out.blocks.push_back(std::move(b));
    }

    // The error covariance, inverted draw by draw (and period by period where
    // it moves). A quantile model's precisions are latent mixture weights and
    // describe no covariance.
    if (!ald)
    {
        const Matrix sigmaInv = readMatrix(file, QStringLiteral("/posterior/u_sigma_inv/coeffs"));
        const std::size_t kk = static_cast<std::size_t>(k * k);
        const bool sigmaMoves = sigmaInv.rows == kk * static_cast<std::size_t>(tt) && tt > 1;
        const int sigmaPeriods = sigmaMoves ? tt : 1;
        // Sigma of every draw and period, (i, j) at [((t * nd) + d) * kk + j * k + i].
        std::vector<double> sigma(static_cast<std::size_t>(sigmaPeriods) * nd * kk);
        const auto ku = static_cast<arma::uword>(k);
        for (int t = 0; t < sigmaPeriods; ++t)
        {
            for (std::size_t d = 0; d < nd; ++d)
            {
                arma::mat precision(ku, ku);
                for (std::size_t e = 0; e < kk; ++e)
                {
                    precision(e % ku, e / ku) = sigmaInv.at(static_cast<std::size_t>(t) * kk + e, d);
                }
                const arma::mat cov = arma::inv_sympd(arma::symmatu(precision));
                std::copy(cov.begin(), cov.end(), sigma.begin() + static_cast<std::ptrdiff_t>((t * nd + d) * kk));
            }
        }
        CoefficientBlock b;
        b.title = QObject::tr("Covariance matrix of the error term");
        b.columns = out.equations;
        for (int j = 0; j < k; ++j)
        {
            for (int i = 0; i < k; ++i)
            {
                const std::size_t e = static_cast<std::size_t>(j * k + i);
                b.panels.push_back(panel(sigmaPeriods, [&](int t, std::size_t d) {
                    return sigma[(static_cast<std::size_t>(t) * nd + d) * kk + e];
                }));
            }
        }
        out.blocks.push_back(std::move(b));
    }
    // The covariance block's own selection: /posterior/psi/lambda holds the
    // whole of Psi per draw, vectorised; its free elements below the diagonal
    // are numbered row by row in /priors/psi/include.
    bool havePsiLambda = false;
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        havePsiLambda = h5.exist("/posterior/psi/lambda");
    }
    if (havePsiLambda)
    {
        const Matrix lambda = readMatrix(file, QStringLiteral("/posterior/psi/lambda"));
        const auto ku = static_cast<arma::uword>(k);
        if (lambda.rows == static_cast<std::size_t>(k * k) && lambda.cols > 0)
        {
            out.psiInclusion.zeros(ku, ku);
            out.psiSelected.zeros(ku, ku);
            for (std::size_t e = 0; e < lambda.rows; ++e)
            {
                double sum = 0.0;
                for (std::size_t d = 0; d < lambda.cols; ++d)
                {
                    sum += lambda.at(e, d);
                }
                out.psiInclusion(e % ku, e / ku) = sum / static_cast<double>(lambda.cols);
            }
            Matrix include;
            try
            {
                include = readMatrix(file, QStringLiteral("/priors/psi/include"));
            }
            catch (const std::exception &)
            {
            }
            for (const double v : include.values)
            {
                // Free element n (one-based), row by row: row r holds r of them.
                int n = static_cast<int>(v) - 1;
                for (arma::uword r = 1; r < ku && n >= 0; ++r)
                {
                    if (n < static_cast<int>(r))
                    {
                        out.psiSelected(r, static_cast<arma::uword>(n)) = 1;
                        break;
                    }
                    n -= static_cast<int>(r);
                }
            }
        }
    }
    return out;
}

} // namespace studio
