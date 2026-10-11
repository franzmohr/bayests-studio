// The studio's impulse responses and variance decompositions against
// bvartools', draw by draw, on model files bvartools estimated and wrote.
//
//   structural_test <dir>
//
// <dir> is what test/structural_reference.R writes. Every response draw has to
// agree with bvartools' to rounding, and the posterior mean decomposition too.

#include "engine/ModelFileTree.h"
#include "engine/CoefficientSummary.h"
#include "engine/DataSet.h"
#include "engine/Multipliers.h"
#include "engine/VarBuilder.h"
#include "engine/SignZero.h"
#include "engine/Spillover.h"
#include "engine/Structural.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <limits>
#include <cstdio>

using namespace studio;

namespace
{

int failures = 0;

/// A CSV of numbers with a header row: rows x columns.
arma::mat readCsv(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error("cannot open " + path.toStdString());
    }
    QTextStream in(&f);
    in.readLine();
    std::vector<std::vector<double>> rows;
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
        {
            continue;
        }
        std::vector<double> row;
        for (const QString &cell : line.split(','))
        {
            row.push_back(cell.toDouble());
        }
        rows.push_back(row);
    }
    arma::mat m(rows.size(), rows.front().size());
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        for (std::size_t c = 0; c < rows[r].size(); ++c)
        {
            m(r, c) = rows[r][c];
        }
    }
    return m;
}

int indexOf(const StructuralSupport &support, const QString &name)
{
    const int i = static_cast<int>(support.variables.indexOf(name));
    if (i < 0)
    {
        throw std::runtime_error("no variable " + name.toStdString());
    }
    return i;
}

void compareIrf(const QString &dir, const QString &model, ResponseType type, const QString &typeName,
                const QString &impulse, const QString &response)
{
    const QString file = dir + "/" + model + ".h5";
    const StructuralSupport support = structuralSupport(file);
    if (!support.ok)
    {
        std::printf("FAIL  %s: %s\n", model.toStdString().c_str(), support.reason.toStdString().c_str());
        ++failures;
        return;
    }
    const arma::mat reference =
        readCsv(QStringLiteral("%1/%2_irf_%3_%4_%5.csv").arg(dir, model, typeName, impulse, response));
    ResponseRequest request;
    request.type = type;
    request.horizon = static_cast<int>(reference.n_cols) - 1;
    request.maxDraws = 1 << 30;   // every draw; unidentified ones drop out on both sides
    const auto draws = impulseResponses(file, request, indexOf(support, impulse));
    const auto j = static_cast<arma::uword>(indexOf(support, response));
    double worst = 0.0;
    for (std::size_t d = 0; d < draws.size(); ++d)
    {
        for (arma::uword h = 0; h < reference.n_cols; ++h)
        {
            worst = std::max(worst, std::abs(draws[d](h, j) - reference(d, h)));
        }
    }
    const bool ok = draws.size() == reference.n_rows && worst < 1e-9;
    std::printf("%s  %s %s %s -> %s: %zu draws, max difference %.2e\n", ok ? "ok  " : "FAIL",
                model.toStdString().c_str(), typeName.toStdString().c_str(),
                impulse.toStdString().c_str(), response.toStdString().c_str(), draws.size(), worst);
    failures += ok ? 0 : 1;
}

void compareFevd(const QString &dir, const QString &model, ResponseType type, const QString &typeName,
                 const QString &response)
{
    const QString file = dir + "/" + model + ".h5";
    const StructuralSupport support = structuralSupport(file);
    const arma::mat reference =
        readCsv(QStringLiteral("%1/%2_fevd_%3_%4.csv").arg(dir, model, typeName, response));
    ResponseRequest request;
    request.type = type;
    request.horizon = static_cast<int>(reference.n_rows) - 1;
    request.maxDraws = 1 << 30;
    const auto draws = varianceDecomposition(file, request, indexOf(support, response));
    arma::mat mean(reference.n_rows, reference.n_cols, arma::fill::zeros);
    for (const auto &d : draws)
    {
        mean += d;
    }
    mean /= static_cast<double>(draws.size());
    const double worst = arma::abs(mean - reference).max();
    const bool ok = worst < 1e-9;
    std::printf("%s  %s fevd %s of %s: max difference %.2e\n", ok ? "ok  " : "FAIL",
                model.toStdString().c_str(), typeName.toStdString().c_str(),
                response.toStdString().c_str(), worst);
    failures += ok ? 0 : 1;
}

/// The connectedness measures against bvartools' spillover(), draw by draw:
/// the total index, from, to and net of every draw, and the posterior mean
/// table with its margins.
void compareSpillover(const QString &dir, const QString &name, const QString &model, SpilloverType type,
                      int horizon, int period = -1)
{
    const QString csv = QStringLiteral("%1/spillover_%2.csv").arg(dir, name);
    if (!QFile::exists(csv))
    {
        std::printf("skip  spillover %s: run test/spillover_reference.R first\n", name.toStdString().c_str());
        return;
    }
    const arma::mat reference = readCsv(csv);
    const arma::mat table = readCsv(QStringLiteral("%1/spillover_%2_table.csv").arg(dir, name));
    const arma::mat pairwise = readCsv(QStringLiteral("%1/spillover_%2_pairwise.csv").arg(dir, name));
    SpilloverRequest request;
    request.type = type;
    request.horizon = horizon;
    request.period = period;
    request.maxDraws = 1 << 30;
    const Spillovers s = spillovers(dir + "/" + model + ".h5", request);
    const arma::uword k = s.from.n_cols;
    double worst = 0.0;
    if (s.total.n_elem == reference.n_rows)
    {
        const arma::mat ours = arma::join_rows(arma::join_rows(arma::mat(s.total), s.from), arma::join_rows(s.to, s.net));
        worst = arma::abs(ours - reference).max();
    }
    // The printed table: shares, the from column, the to row, the total in the corner.
    arma::mat printed(k + 1, k + 1);
    printed.submat(0, 0, k - 1, k - 1) = s.table;
    printed.submat(0, k, k - 1, k) = arma::trans(arma::mean(s.from, 0));
    printed.submat(k, 0, k, k - 1) = arma::mean(s.to, 0);
    printed(k, k) = arma::mean(s.total);
    const double worstTable = std::max(arma::abs(printed - table).max(), arma::abs(s.pairwise - pairwise).max());
    const double identity = arma::abs(arma::sum(s.from, 1) - s.total).max() + arma::abs(arma::sum(s.to, 1) - s.total).max();
    const bool ok = s.total.n_elem == reference.n_rows && worst < 1e-9 && worstTable < 1e-9 && identity < 1e-9;
    std::printf("%s  spillover %s: %u draws, max difference %.2e, table %.2e, median total %.2f\n",
                ok ? "ok  " : "FAIL", name.toStdString().c_str(), static_cast<unsigned>(s.total.n_elem),
                std::max(worst, identity), worstTable, arma::median(s.total));
    failures += ok ? 0 : 1;
}

/// The summary plot's panels against bvartools' summary(): block by block,
/// column by column, equation by equation -- the order of summary()'s columns
/// -- each panel's mean, median and 95% bounds, A_0's fixed ones and zeros
/// included, then the error covariance.
void compareSummary(const QString &dir, const QString &model)
{
    const QString csv = QStringLiteral("%1/summary_%2.csv").arg(dir, model);
    if (!QFile::exists(csv))
    {
        std::printf("skip  summary %s: run test/summary_reference.R first\n", model.toStdString().c_str());
        return;
    }
    // The first two columns are text; the reader turns them into zeros.
    const arma::mat reference = readCsv(csv);
    const CoefficientSummary s = coefficientSummary(dir + "/" + model + ".h5");
    std::vector<arma::rowvec> ours;
    for (const CoefficientBlock &b : s.blocks)
    {
        for (const CoefficientPanel &p : b.panels)
        {
            if (p.kind == CoefficientPanel::Kind::Fixed)
            {
                ours.push_back({p.value, p.value, p.value, p.value});
                continue;
            }
            std::vector<double> v(p.draws.begin(), p.draws.end());
            std::sort(v.begin(), v.end());
            const auto q = [&](double pr) {
                const double h = (static_cast<double>(v.size()) - 1.0) * pr;
                const auto lo = static_cast<std::size_t>(std::floor(h));
                const std::size_t hi = std::min(lo + 1, v.size() - 1);
                return v[lo] + (h - static_cast<double>(lo)) * (v[hi] - v[lo]);
            };
            ours.push_back({arma::mean(p.draws), q(0.5), q(0.025), q(0.975)});
        }
    }
    double worst = 0.0;
    if (ours.size() == reference.n_rows)
    {
        for (std::size_t r = 0; r < ours.size(); ++r)
        {
            worst = std::max(worst, arma::abs(ours[r] - reference.row(r).cols(3, 6)).max());
        }
    }
    const bool ok = ours.size() == reference.n_rows && worst < 1e-9;
    std::printf("%s  summary %s: %zu blocks, %zu panels (bvartools %u), max difference %.2e\n", ok ? "ok  " : "FAIL",
                model.toStdString().c_str(), s.blocks.size(), ours.size(), static_cast<unsigned>(reference.n_rows),
                worst);
    failures += ok ? 0 : 1;
}

/// The dynamic multipliers against bvartools' multipliers(), every draw:
/// permanent and transitory, for each response the reference script wrote.
void compareMultipliers(const QString &dir, const QString &model, const QString &impulse,
                        const QStringList &responses, int period = -1)
{
    const QString file = dir + "/" + model + ".h5";
    if (!QFile::exists(file))
    {
        std::printf("skip  multipliers %s: run test/exogenous_reference.R first\n", model.toStdString().c_str());
        return;
    }
    const MultiplierSupport support = multiplierSupport(file);
    if (!support.ok)
    {
        std::printf("FAIL  multipliers %s: %s\n", model.toStdString().c_str(), support.reason.toStdString().c_str());
        ++failures;
        return;
    }
    for (const bool permanent : {true, false})
    {
        for (const QString &response : responses)
        {
            const arma::mat reference = readCsv(QStringLiteral("%1/multipliers_%2_%3_%4_%5.csv")
                                                    .arg(dir, model, permanent ? "permanent" : "transitory",
                                                         impulse, response));
            MultiplierRequest request;
            request.impulse = static_cast<int>(support.exogenous.indexOf(impulse));
            request.horizon = static_cast<int>(reference.n_cols) - 1;
            request.permanent = permanent;
            request.period = period;
            request.maxDraws = 1 << 30;
            const auto draws = dynamicMultipliers(file, request);
            const auto j = static_cast<arma::uword>(support.variables.indexOf(response));
            double worst = 0.0;
            for (std::size_t d = 0; d < draws.size() && d < reference.n_rows; ++d)
            {
                for (arma::uword h = 0; h < reference.n_cols; ++h)
                {
                    worst = std::max(worst, std::abs(draws[d](h, j) - reference(d, h)));
                }
            }
            const bool ok = draws.size() == reference.n_rows && worst < 1e-9;
            std::printf("%s  multipliers %s %s %s -> %s: %zu draws, max difference %.2e\n", ok ? "ok  " : "FAIL",
                        model.toStdString().c_str(), permanent ? "permanent" : "transitory",
                        impulse.toStdString().c_str(), response.toStdString().c_str(), draws.size(), worst);
            failures += ok ? 0 : 1;
        }
    }
}

/// The builder's data against bvartools' for the same model with exogenous
/// variables: the response, the SUR regressors, a VEC's error correction
/// term and the forecast regressors, element by element.
void compareBuilderData(const QString &dir, const QString &model, const QString &csv, VarBuilderSpec spec)
{
    const QString reference = dir + "/" + model + ".h5";
    if (!QFile::exists(reference))
    {
        std::printf("skip  builder %s: run test/exogenous_reference.R first\n", model.toStdString().c_str());
        return;
    }
    spec.data = readDelimited(dir + "/" + csv);
    QTemporaryDir tmp;
    const QString built = tmp.filePath(QStringLiteral("built.h5"));
    writeVarModel(spec, built);
    double worst = 0.0;
    QString where;
    for (const char *path : {"/data/train/y", "/data/train/z", "/data/train/w", "/data/forecast/x", "/data/test/y"})
    {
        const bool vecOnly = std::string(path) == "/data/train/w";
        if (vecOnly && !spec.vec())
        {
            continue;
        }
        Matrix b;
        try
        {
            b = readMatrix(reference, QString::fromLatin1(path));
        }
        catch (const std::exception &)
        {
            continue;   // not part of bvartools' file
        }
        const Matrix a = readMatrix(built, QString::fromLatin1(path));
        if (a.rows != b.rows || a.cols != b.cols)
        {
            std::printf("FAIL  builder %s: %s is (%zu, %zu), bvartools' (%zu, %zu)\n", model.toStdString().c_str(),
                        path, a.rows, a.cols, b.rows, b.cols);
            ++failures;
            return;
        }
        for (std::size_t i = 0; i < a.values.size(); ++i)
        {
            const double gap = std::abs(a.values[i] - b.values[i]);
            if (gap > worst)
            {
                worst = gap;
                where = QString::fromLatin1(path);
            }
        }
    }
    const bool ok = worst < 1e-9;
    std::printf("%s  builder %s: y, z%s, and forecast x as bvartools writes them, max difference %.2e%s\n",
                ok ? "ok  " : "FAIL", model.toStdString().c_str(), spec.vec() ? ", w" : "", worst,
                ok ? "" : (" in " + where.toStdString()).c_str());
    failures += ok ? 0 : 1;
}

/// The studio's own rotation search on the VAR bvartools estimated: every
/// accepted rotation orthonormal and satisfying every restriction, and the
/// identified responses distributed as bvartools' -- both draw uniformly on
/// the orthogonal group, so their medians agree up to Monte Carlo error.
void ownIdentification(const QString &dir)
{
    QTemporaryDir tmp;
    const QString file = tmp.filePath(QStringLiteral("var2.h5"));
    QFile::copy(dir + "/var2.h5", file);
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const StructuralSupport support = structuralSupport(file);
    const int income = indexOf(support, "income");
    const int cons = indexOf(support, "cons");

    SignIdentification spec;
    spec.restrictions = {{income, income, 1, 0}, {income, cons, 1, 0}, {cons, cons, 1, 0}};
    spec.maxTries = 500;
    const SignIdentification done = identifySigns(file, spec, 20261011ULL);

    const auto stored = readSignIdentification(file);
    if (!stored || stored->restrictions.size() != 3 || stored->accepted != done.accepted)
    {
        std::printf("FAIL  the identification does not read back as written\n");
        ++failures;
        return;
    }

    // Every identified draw: Q orthonormal, impact responses of the right sign.
    ResponseRequest request;
    request.type = ResponseType::Sign;
    request.horizon = 8;
    request.maxDraws = 1 << 30;
    const auto toIncome = impulseResponses(file, request, income);
    const auto toCons = impulseResponses(file, request, cons);
    int violations = 0;
    for (std::size_t d = 0; d < toIncome.size(); ++d)
    {
        violations += toIncome[d](0, income) > 0 ? 0 : 1;
        violations += toIncome[d](0, cons) > 0 ? 0 : 1;
        violations += toCons[d](0, cons) > 0 ? 0 : 1;
    }
    const Matrix q = readMatrix(file, QStringLiteral("/posterior/q/coeffs"));
    double worstOrthogonality = 0.0;
    const auto k = static_cast<arma::uword>(support.k);
    for (std::size_t d = 0; d < q.cols; ++d)
    {
        if (std::isnan(q.at(0, d)))
        {
            continue;
        }
        arma::mat Q(k, k);
        for (std::size_t i = 0; i < k * k; ++i)
        {
            Q(i % k, i / k) = q.at(i, d);
        }
        worstOrthogonality = std::max(worstOrthogonality, arma::abs(Q.t() * Q - arma::eye(k, k)).max());
    }

    // Medians against bvartools', relative to the width of the 68% band.
    const arma::mat reference = readCsv(dir + "/sign_irf_sign_income_cons.csv");
    double worstGap = 0.0;
    for (int h = 0; h <= 8; ++h)
    {
        std::vector<double> ours, theirs;
        for (const auto &m : toIncome)
        {
            ours.push_back(m(static_cast<arma::uword>(h), static_cast<arma::uword>(cons)));
        }
        for (arma::uword d = 0; d < reference.n_rows; ++d)
        {
            theirs.push_back(reference(d, static_cast<arma::uword>(h)));
        }
        std::sort(ours.begin(), ours.end());
        std::sort(theirs.begin(), theirs.end());
        const auto q = [](const std::vector<double> &v, double p) { return v[static_cast<std::size_t>(p * (v.size() - 1))]; };
        const double width = std::max(q(theirs, 0.84) - q(theirs, 0.16), 1e-12);
        worstGap = std::max(worstGap, std::abs(q(ours, 0.5) - q(theirs, 0.5)) / width);
    }
    const bool ok = done.accepted > 0 && violations == 0 && worstOrthogonality < 1e-10 && worstGap < 0.25;
    std::printf("%s  own identification: %d of %d draws, %d violations, |Q'Q - I| %.1e, "
                "median gap to bvartools %.3f of the 68%% band\n",
                ok ? "ok  " : "FAIL", done.accepted, done.draws, violations, worstOrthogonality, worstGap);
    failures += ok ? 0 : 1;

    // Kept for the R side of the round trip (test/structural_reference.R
    // does not need it; a reader can open it in bvartools).
    QFile::remove(dir + "/studio_sign.h5");
    QFile::copy(file, dir + "/studio_sign.h5");
}

/// Sign and zero restrictions (Arias, Rubio-Ramírez and Waggoner 2018), held
/// to bvartools four ways: the Pareto smoother exactly, responses from
/// bvartools' rotations exactly, the volume element by the identity it must
/// satisfy, and the studio's own identification within Monte Carlo error.
void signZeroChecks(const QString &dir)
{
    if (!QFile::exists(dir + "/psis.csv"))
    {
        std::printf("skip  sign and zero restrictions: run test/signzero_reference.R first\n");
        return;
    }

    // 1. The smoother, on the same log weights.
    {
        const arma::mat psis = readCsv(dir + "/psis.csv");
        const double referenceK = readCsv(dir + "/psis_k.csv")(0, 0);
        std::vector<double> lw(psis.n_rows);
        for (arma::uword i = 0; i < psis.n_rows; ++i)
        {
            lw[i] = psis(i, 0);
        }
        double k = 0.0;
        const std::vector<double> smoothed = psisSmooth(lw, k);
        double worst = 0.0;
        for (arma::uword i = 0; i < psis.n_rows; ++i)
        {
            worst = std::max(worst, std::abs(smoothed[i] - psis(i, 1)));
        }
        const bool ok = worst < 1e-10 && std::abs(k - referenceK) < 1e-10;
        std::printf("%s  Pareto smoothing: max difference %.2e, k %.6f against %.6f\n", ok ? "ok  " : "FAIL", worst,
                    k, referenceK);
        failures += ok ? 0 : 1;
    }

    // 2. Responses from bvartools' own sign and zero rotations.
    compareIrf(dir, "signzero", ResponseType::Sign, "sign", "invest", "income");
    compareIrf(dir, "signzero", ResponseType::Sign, "sign", "invest", "cons");
    compareIrf(dir, "signzero", ResponseType::Sign, "sign", "income", "cons");
    compareFevd(dir, "signzero", ResponseType::Sign, "sign", "cons");

    const QString var2 = dir + "/var2.h5";
    const StructuralSupport support = structuralSupport(var2);
    const int invest = indexOf(support, "invest");
    const int income = indexOf(support, "income");
    const int cons = indexOf(support, "cons");

    // 3. With signs alone the two volume elements -- one in closed form, one
    // by numerical differentiation of Algorithm 2 read backwards -- cancel:
    // every weight is the same up to the derivative's rounding.
    {
        const std::vector<ZeroSignRestriction> signsOnly = {{income, income, 1, 0.0}, {income, cons, 1, 0.0}};
        const std::vector<double> lw = signZeroLogWeights(var2, signsOnly, 7, 150);
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        int finite = 0;
        for (const double v : lw)
        {
            if (std::isfinite(v))
            {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                ++finite;
            }
        }
        const bool ok = finite > 20 && hi - lo < 1e-5;
        std::printf("%s  volume elements cancel under signs alone: %d draws, log weights within %.2e\n",
                    ok ? "ok  " : "FAIL", finite, hi - lo);
        failures += ok ? 0 : 1;
    }

    // 4. The studio's own identification, as bvartools' was made.
    {
        QTemporaryDir tmp;
        const QString output = dir + "/studio_signzero.h5";
        SignZeroSpec spec;
        spec.restrictions = {{invest, invest, 1, 0.0}, {invest, income, 0, 0.0}, {income, income, 1, 0.0},
                             {income, cons, 1, 0.0}};
        spec.draws = 1000;
        const SignZeroResult res = identifySignZero(var2, output, spec, 20261012ULL);
        const arma::mat summary = readCsv(dir + "/signzero_summary.csv");
        const double theirAccepted = summary(0, 0);
        const double theirEffective = summary(0, 1);

        ResponseRequest request;
        request.type = ResponseType::Sign;
        request.horizon = 8;
        request.maxDraws = 1 << 30;
        const auto toInvest = impulseResponses(output, request, invest);
        const auto toIncome = impulseResponses(output, request, income);
        double worstZero = 0.0;
        int violations = 0;
        for (std::size_t d = 0; d < toInvest.size(); ++d)
        {
            worstZero = std::max(worstZero, std::abs(toInvest[d](0, static_cast<arma::uword>(income))));
            violations += toInvest[d](0, static_cast<arma::uword>(invest)) > 0 ? 0 : 1;
            violations += toIncome[d](0, static_cast<arma::uword>(income)) > 0 ? 0 : 1;
            violations += toIncome[d](0, static_cast<arma::uword>(cons)) > 0 ? 0 : 1;
        }
        // Accepted draws: binomial around bvartools' rate; the effective
        // sample size within a factor the importance sampler's noise allows.
        const double rate = theirAccepted / 1000.0;
        const double sd = std::sqrt(1000.0 * rate * (1.0 - rate));
        const bool acceptance = std::abs(res.accepted - theirAccepted) < 5.0 * sd;
        const bool efficiency = res.effectiveSampleSize > 0.5 * theirEffective && res.effectiveSampleSize < 2.0 * theirEffective;

        // Medians of the identified responses against bvartools', relative
        // to the 68% band.
        double worstGap = 0.0;
        for (const auto &[shock, response, name] :
             {std::tuple{invest, cons, "invest_cons"}, std::tuple{income, cons, "income_cons"}})
        {
            const auto ours = impulseResponses(output, request, shock);
            const arma::mat theirs = readCsv(QStringLiteral("%1/signzero_irf_sign_%2.csv").arg(dir, name));
            for (int h = 0; h <= 8; ++h)
            {
                std::vector<double> a, b;
                for (const auto &m : ours)
                {
                    a.push_back(m(static_cast<arma::uword>(h), static_cast<arma::uword>(response)));
                }
                for (arma::uword d = 0; d < theirs.n_rows; ++d)
                {
                    b.push_back(theirs(d, static_cast<arma::uword>(h)));
                }
                std::sort(a.begin(), a.end());
                std::sort(b.begin(), b.end());
                const auto q = [](const std::vector<double> &v, double p) { return v[static_cast<std::size_t>(p * (v.size() - 1))]; };
                const double width = std::max(q(b, 0.84) - q(b, 0.16), 1e-12);
                worstGap = std::max(worstGap, std::abs(q(a, 0.5) - q(b, 0.5)) / width);
            }
        }
        const bool ok = worstZero < 1e-10 && violations == 0 && acceptance && efficiency &&
                        !(res.paretoK >= 0.7) && worstGap < 0.3;
        std::printf("%s  own sign and zero identification: %d of %d accepted (bvartools %.0f), effective %d "
                    "(bvartools %.0f), Pareto k %.3f, zero restriction within %.1e, %d sign violations, "
                    "median gap %.3f of the 68%% band\n",
                    ok ? "ok  " : "FAIL", res.accepted, res.candidates, theirAccepted, res.effectiveSampleSize,
                    theirEffective, res.paretoK, worstZero, violations, worstGap);
        failures += ok ? 0 : 1;

        // The round trip, once bvartools has read the studio's file.
        if (QFile::exists(dir + "/studio_signzero_irf_sign_invest_cons.csv"))
        {
            compareIrf(dir, "studio_signzero", ResponseType::Sign, "sign", "invest", "cons");
            compareFevd(dir, "studio_signzero", ResponseType::Sign, "sign", "cons");
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::printf("usage: structural_test <reference dir>\n");
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    try
    {
        compareIrf(dir, "var2", ResponseType::Feir, "feir", "income", "cons");
        compareIrf(dir, "var2", ResponseType::Oir, "oir", "income", "cons");
        compareIrf(dir, "var2", ResponseType::Gir, "gir", "invest", "income");
        compareFevd(dir, "var2", ResponseType::Oir, "oir", "cons");
        compareIrf(dir, "sign", ResponseType::Sign, "sign", "income", "cons");
        compareIrf(dir, "sign", ResponseType::Sign, "sign", "cons", "invest");
        compareFevd(dir, "sign", ResponseType::Sign, "sign", "cons");
        compareIrf(dir, "svar", ResponseType::Sir, "sir", "invest", "cons");
        compareIrf(dir, "svar", ResponseType::Sgir, "sgir", "income", "invest");
        compareFevd(dir, "svar", ResponseType::Sir, "sir", "cons");
        compareIrf(dir, "vec", ResponseType::Oir, "oir", "R", "Dp");
        compareFevd(dir, "vec", ResponseType::Oir, "oir", "Dp");
        compareIrf(dir, "tvp", ResponseType::Oir, "oir", "income", "cons");
        compareFevd(dir, "tvp", ResponseType::Oir, "oir", "cons");
        compareSpillover(dir, "var2_gir", "var2", SpilloverType::Gir, 10);
        compareSpillover(dir, "var2_oir", "var2", SpilloverType::Oir, 4);
        compareSpillover(dir, "sign", "sign", SpilloverType::Sign, 10);
        compareSpillover(dir, "vec_gir", "vec", SpilloverType::Gir, 10);
        compareSpillover(dir, "tvp_gir_last", "tvp", SpilloverType::Gir, 10);
        compareSpillover(dir, "tvp_gir_10", "tvp", SpilloverType::Gir, 10, 10);
        compareSummary(dir, "var2");
        compareSummary(dir, "vec");
        compareSummary(dir, "svar");
        {
            VarBuilderSpec varx;
            varx.columns = {0, 1};
            varx.exogenous = {2};
            varx.p = 2;
            varx.s = 1;
            varx.horizon = 4;
            varx.holdout = 4;
            compareBuilderData(dir, "varx", "e1.csv", varx);
            VarBuilderSpec vecx = varx;
            vecx.family = VarBuilderSpec::Family::Vec;
            vecx.s = 2;
            vecx.rank = 1;
            compareBuilderData(dir, "vecx", "e6x.csv", vecx);
        }
        compareMultipliers(dir, "varx", "cons", {"invest", "income"});
        compareMultipliers(dir, "vecx", "g", {"R", "Dp"});
        compareMultipliers(dir, "tvpx", "cons", {"income"}, 20);
        ownIdentification(dir);
        signZeroChecks(dir);
        // The round trip: bvartools reading the studio's identification. The
        // CSVs come from bvartools on studio_sign.h5, which the line above
        // writes; present only once that has been run.
        if (QFile::exists(dir + "/studio_sign_irf_sign_income_cons.csv"))
        {
            compareIrf(dir, "studio_sign", ResponseType::Sign, "sign", "income", "cons");
            compareFevd(dir, "studio_sign", ResponseType::Sign, "sign", "cons");
        }
    }
    catch (const std::exception &e)
    {
        std::printf("FAIL  %s\n", e.what());
        ++failures;
    }
    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
