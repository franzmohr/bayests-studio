// The model builder against BayesTS itself.
//
// Every combination the builder's form allows is written from one simulated
// data set, checked by `bayests check` (ModelRunner::check) and run through
// all three stages by the registered front-ends -- the run the command line
// would make. A combination that BayesTS refuses, or whose stages leave a
// result out, fails the test. Two models are then held to the numbers: the
// coefficients the data were simulated from, and least squares.

#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"
#include "engine/VarBuilder.h"

#include <QDir>
#include <QTemporaryDir>

#include <highfive/highfive.hpp>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "bayests/reporter.h"
#include "model_seed.h"
#include "models/models.h"

using namespace studio;

namespace
{

int failures = 0;

void fail(const std::string &what)
{
    std::printf("FAIL  %s\n", what.c_str());
    ++failures;
}

/// A stable VAR(1) with an intercept: y_t = c + A y_{t-1} + e_t.
DataSet simulate(arma::mat &A, arma::vec &c)
{
    const int k = 3;
    const int T = 160;
    A = {{0.5, 0.1, 0.0}, {0.0, 0.4, 0.2}, {0.1, 0.0, 0.3}};
    c = {1.0, -0.5, 0.2};
    std::mt19937_64 rng(20261011ULL);
    std::normal_distribution<double> normal(0.0, 1.0);
    arma::mat y(T, k, arma::fill::zeros);
    y.row(0) = arma::trans(arma::solve(arma::eye(k, k) - A, c));
    for (int t = 1; t < T; ++t)
    {
        arma::vec e(k);
        for (int i = 0; i < k; ++i)
        {
            e(i) = 0.5 * normal(rng);
        }
        y.row(t) = arma::trans(c + A * y.row(t - 1).t() + e);
    }
    DataSet data;
    data.source = QStringLiteral("simulated");
    data.names = {QStringLiteral("y1"), QStringLiteral("y2"), QStringLiteral("y3")};
    for (int t = 0; t < T; ++t)
    {
        data.periods << QStringLiteral("t%1").arg(t + 1);
    }
    data.values = y;
    return data;
}

/// Three series in levels: y1 and y3 random walks, y2 = y1 plus a stationary
/// AR(1), so the one cointegrating vector is proportional to (1, -1, 0).
DataSet simulateCointegrated()
{
    const int T = 300;
    std::mt19937_64 rng(20261012ULL);
    std::normal_distribution<double> normal(0.0, 1.0);
    arma::mat y(T, 3, arma::fill::zeros);
    double gap = 0.0;
    y(0, 0) = 10.0;
    y(0, 2) = 5.0;
    y(0, 1) = 10.0;
    for (int t = 1; t < T; ++t)
    {
        y(t, 0) = y(t - 1, 0) + 0.2 + normal(rng);
        gap = 0.5 * gap + 0.5 * normal(rng);
        y(t, 1) = y(t, 0) + gap;
        y(t, 2) = y(t - 1, 2) + normal(rng);
    }
    DataSet data;
    data.source = QStringLiteral("simulated, cointegrated");
    data.names = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
    for (int t = 0; t < T; ++t)
    {
        data.periods << QStringLiteral("%1 Q%2").arg(1950 + t / 4).arg(t % 4 + 1);
    }
    data.values = y;
    return data;
}

bool exists(const QString &file, const std::string &path)
{
    HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    return h5.exist(path);
}

/// Every stage, as ModelRunner runs it, with nothing listening.
void runAll(const QString &file, std::uint64_t seed)
{
    const ModelLocation location{std::filesystem::path(file.toStdWString()), std::string()};
    const CheckReport report = ModelRunner::check(file, QString());
    bayests::NullReporter quiet;
    for (const ModelStage stage : {ModelStage::coefficients, ModelStage::forecast, ModelStage::log_likelihood})
    {
        auto model = create_model(report.algorithm.toStdString());
        model->set_reporter(&quiet);
        seed_model_stage(seed, stage);
        switch (stage)
        {
        case ModelStage::coefficients: model->draw_coefficients(location); break;
        case ModelStage::forecast: model->forecast(location); break;
        case ModelStage::log_likelihood: model->log_likelihood(location); break;
        }
    }
}

arma::mat readDraws(const QString &file, const std::string &path)
{
    HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    const HighFive::DataSet ds = h5.getDataSet(path);
    const auto dims = ds.getSpace().getDimensions();
    // Dataspace (rows, cols) row-major is an Armadillo (cols, rows) matrix
    // column-major: transpose back to one row per quantity.
    arma::mat m(dims[1], dims[0]);
    ds.read_raw(m.memptr());
    return m.t();
}

} // namespace

int main()
{
    arma::mat A;
    arma::vec c;
    const DataSet data = simulate(A, c);
    const DataSet levels = simulateCointegrated();
    QTemporaryDir dir;
    using Family = VarBuilderSpec::Family;

    using Errors = VarBuilderSpec::Errors;
    using Selection = VarBuilderSpec::Selection;

    // -- Every combination the form allows.
    int written = 0;
    for (const Family family : {Family::Var, Family::Vec})
    {
    for (const bool tvp : {false, true})
    {
        for (const Errors errors : {Errors::Wishart, Errors::Gamma, Errors::Stochvol})
        {
            for (const bool covar : {false, true})
            {
                for (const bool structural : {false, true})
                {
                    for (const Selection selection : {Selection::None, Selection::Bvs, Selection::Ssvs})
                    {
                        for (const bool nonCentred : {false, true})
                        {
                            VarBuilderSpec s;
                            s.family = family;
                            s.data = family == Family::Vec ? levels : data;
                            s.columns = {0, 1, 2};
                            s.p = 2;
                            if (family == Family::Vec)
                            {
                                // The deterministic cases rotate: an unrestricted
                                // constant; the constant in the space; an
                                // unrestricted constant with the trend in it.
                                const int variant = written % 3;
                                s.restrictConstant = variant == 1;
                                s.trend = variant == 2;
                                s.restrictTrend = true;
                                s.rank = 1 + written % 2;
                            }
                            s.timeVarying = tvp;
                            s.errors = errors;
                            s.covar = covar;
                            s.structural = structural;
                            s.selection = selection;
                            s.selectCovariances = covar && selection != Selection::None;
                            s.nonCentred = nonCentred;
                            s.minnesota = (written % 2) == 1;   // half of them under the Minnesota prior
                            s.iterations = 60;
                            s.burnin = 40;
                            s.seed = 7;
                            s.horizon = 4;
                            s.holdout = structural ? 0 : 2;   // a structural model cannot be scored
                            if (!tvp && nonCentred)
                            {
                                continue;   // the form only offers it with a random walk
                            }
                            const VarBuilderSummary summary = summarise(s);
                            if (!summary.refusal.isEmpty())
                            {
                                continue;   // refused by the form, as BayesTS would
                            }
                            const QString name = QStringLiteral("%1-r%7-d%8-c%2-s%3-v%4-n%5-m%6.h5")
                                                     .arg(s.algorithm())
                                                     .arg(covar).arg(structural)
                                                     .arg(static_cast<int>(selection))
                                                     .arg(nonCentred).arg(s.minnesota)
                                                     .arg(s.vec() ? s.rank : 0)
                                                     .arg(s.vec() ? written % 3 : 0);
                            const QString file = dir.filePath(name);
                            ++written;
                            try
                            {
                                writeVarModel(s, file);
                                const CheckReport report = ModelRunner::check(file, QString());
                                if (!report.ok)
                                {
                                    fail(name.toStdString() + ": check refuses it: " + report.error.toStdString());
                                    continue;
                                }
                                for (const QString &w : report.warnings)
                                {
                                    fail(name.toStdString() + ": check warns: " + w.toStdString());
                                }
                                if (readStringsAttribute(file, QStringLiteral("/data/train/z"),
                                                         QStringLiteral("coefficients")).size() != summary.nparams)
                                {
                                    fail(name.toStdString() + ": not one name per coefficient");
                                }
                                runAll(file, 7);
                                for (const char *path : {"/posterior/loglik", "/posterior/forecast/forecasts",
                                                         "/posterior/forecast/loglik"})
                                {
                                    if (s.holdout == 0 && std::string(path) == "/posterior/forecast/loglik")
                                    {
                                        continue;
                                    }
                                    if (!exists(file, path))
                                    {
                                        fail(name.toStdString() + ": no " + path);
                                    }
                                }
                                std::printf("ok    %s\n", name.toStdString().c_str());
                            }
                            catch (const std::exception &e)
                            {
                                fail(name.toStdString() + ": " + e.what());
                            }
                        }
                    }
                }
            }
        }
    }
    }
    std::printf("%d combinations written and run\n", written);

    // -- Weakly exogenous variables: every family, coefficient and error
    // specification with the third series moved to the right-hand side,
    // forecast given its values over the held-out horizon.
    int exogenousRuns = 0;
    for (const Family family : {Family::Var, Family::Vec})
    {
        for (const bool tvp : {false, true})
        {
            for (const Errors errors : {Errors::Wishart, Errors::Gamma, Errors::Stochvol})
            {
                VarBuilderSpec s;
                s.family = family;
                s.data = family == Family::Vec ? levels : data;
                s.columns = {0, 1};
                s.exogenous = {2};
                s.p = 2;
                s.s = (exogenousRuns % 2 == 0) ? 1 : (family == Family::Vec ? 2 : 0);
                s.rank = 1;
                s.timeVarying = tvp;
                s.errors = errors;
                s.covar = errors != Errors::Wishart;
                s.minnesota = exogenousRuns % 2 == 1;
                s.iterations = 60;
                s.burnin = 40;
                s.seed = 7;
                s.horizon = 4;
                s.holdout = 4;
                const QString name = QStringLiteral("exogenous-%1-s%2.h5").arg(s.algorithm()).arg(s.s);
                const QString file = dir.filePath(name);
                ++exogenousRuns;
                try
                {
                    const VarBuilderSummary summary = summarise(s);
                    if (!summary.refusal.isEmpty())
                    {
                        fail(name.toStdString() + ": refused: " + summary.refusal.toStdString());
                        continue;
                    }
                    writeVarModel(s, file);
                    const CheckReport report = ModelRunner::check(file, QString());
                    if (!report.ok)
                    {
                        fail(name.toStdString() + ": check refuses it: " + report.error.toStdString());
                        continue;
                    }
                    for (const QString &w : report.warnings)
                    {
                        fail(name.toStdString() + ": check warns: " + w.toStdString());
                    }
                    runAll(file, 7);
                    for (const char *path : {"/posterior/loglik", "/posterior/forecast/forecasts",
                                             "/posterior/forecast/loglik"})
                    {
                        if (!exists(file, path))
                        {
                            fail(name.toStdString() + ": no " + path);
                        }
                    }
                    std::printf("ok    %s\n", name.toStdString().c_str());
                }
                catch (const std::exception &e)
                {
                    fail(name.toStdString() + ": " + e.what());
                }
            }
        }
    }

    // -- Refusals that are the builder's own: a variable on both sides, a
    // VEC's exogenous level order of zero, a forecast past the exogenous data.
    {
        VarBuilderSpec s;
        s.data = data;
        s.columns = {0, 1};
        s.exogenous = {1};
        if (summarise(s).refusal.isEmpty())
        {
            fail("a variable both endogenous and exogenous is not refused");
        }
        s.exogenous = {2};
        s.family = Family::Vec;
        s.data = levels;
        s.s = 0;
        if (summarise(s).refusal.isEmpty())
        {
            fail("a VEC with exogenous level order 0 is not refused");
        }
        s.family = Family::Var;
        s.data = data;
        s.s = 1;
        s.horizon = 4;
        s.holdout = 0;
        if (summarise(s).refusal.isEmpty())
        {
            fail("a forecast past the end of the exogenous data is not refused");
        }
    }

    // -- Recovery: a VAR(1) under a weak prior finds the coefficients the data
    // came from, and its posterior mean sits on least squares.
    {
        VarBuilderSpec s;
        s.data = data;
        s.columns = {0, 1, 2};
        s.p = 1;
        s.coefPrecision = 1e-4;
        s.detPrecision = 1e-4;
        s.iterations = 4000;
        s.burnin = 1000;
        s.seed = 11;
        const QString file = dir.filePath(QStringLiteral("recovery.h5"));
        writeVarModel(s, file);
        runAll(file, 11);
        const arma::vec mean = arma::mean(readDraws(file, "/posterior/a/coeffs"), 1);
        // a = vec([A_1, c]) column-major: A_1 first, column by column.
        const arma::mat A1 = arma::reshape(mean.subvec(0, 8), 3, 3);
        const arma::vec c1 = mean.subvec(9, 11);
        const double errA = arma::abs(A1 - A).max();
        const double errC = arma::abs(c1 - c).max();
        std::printf("recovery: max |A1 - A| = %.3f, max |c - c0| = %.3f\n", errA, errC);
        if (errA > 0.15)
        {
            fail("posterior mean of A1 is not near the simulated A");
        }
        if (errC > 0.35)
        {
            fail("posterior mean of the intercept is not near the simulated one");
        }

        // /initial/a is least squares; under a prior this weak the posterior
        // mean has to agree with it closely.
        const arma::vec ols = arma::vectorise(readDraws(file, "/initial/a"));
        const double gap = arma::abs(mean - ols).max();
        std::printf("posterior mean vs least squares: max gap %.4f\n", gap);
        if (gap > 0.03)
        {
            fail("posterior mean is not near least squares under a flat prior");
        }
    }

    // -- The variable order a user picks is the order in the file.
    {
        VarBuilderSpec s;
        s.data = data;
        s.columns = {2, 0, 1};
        s.p = 1;
        s.iterations = 10;
        s.burnin = 0;
        const QString file = dir.filePath(QStringLiteral("order.h5"));
        writeVarModel(s, file);
        // A time series as bayests-r writes one: (k, tt) in dataspace terms,
        // one row per variable in the chosen order.
        const arma::mat y = readDraws(file, "/data/train/y");
        if (y.n_rows != 3 || std::abs(y(0, 0) - data.values(1, 2)) > 1e-12 ||
            std::abs(y(1, 0) - data.values(1, 0)) > 1e-12)
        {
            fail("/data/train/y is not the chosen variables in their order, one row each");
        }
        if (readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables")) !=
            QStringList{QStringLiteral("y3"), QStringLiteral("y1"), QStringLiteral("y2")})
        {
            fail("/data/train/y does not carry the variable names in their order");
        }
    }

    // -- A VEC finds the space the data were simulated from, and its forecast
    // is in levels.
    {
        VarBuilderSpec s;
        s.family = Family::Vec;
        s.data = levels;
        s.columns = {0, 1, 2};
        s.p = 2;
        s.rank = 1;
        s.coefPrecision = 1e-2;
        s.cointShrinkage = 0.0;   // flat on the loadings, uniform on the space
        s.iterations = 3000;
        s.burnin = 1000;
        s.seed = 13;
        s.horizon = 4;
        const QString file = dir.filePath(QStringLiteral("vec-recovery.h5"));
        writeVarModel(s, file);
        runAll(file, 13);
        // beta is identified up to scale: normalise each draw on its first row.
        const arma::mat beta = readDraws(file, "/posterior/beta/coeffs");   // (3, draws)
        arma::mat normalised = beta.each_row() / beta.row(0);
        const arma::vec mean = arma::median(normalised, 1);
        std::printf("VEC: median of beta normalised on x = (%.3f, %.3f, %.3f); simulated (1, -1, 0)\n",
                    mean(0), mean(1), mean(2));
        if (std::abs(mean(1) + 1.0) > 0.1 || std::abs(mean(2)) > 0.1)
        {
            fail("the VEC does not recover the cointegration space");
        }
        // The Johansen start is already in the right place.
        const arma::vec start = arma::vectorise(readDraws(file, "/initial/beta"));
        if (std::abs(start(1) / start(0) + 1.0) > 0.1)
        {
            fail("the Johansen starting value of beta is not near the simulated space");
        }
        const arma::mat forecast = readDraws(file, "/posterior/forecast/forecasts");   // (h*k, draws)
        const double first = arma::median(forecast.row(0));
        const double last = levels.values(levels.length() - 1, 0);
        std::printf("VEC: first forecast of x %.2f, last level %.2f\n", first, last);
        if (std::abs(first - last) > 5.0)
        {
            fail("the VEC forecast is not in levels");
        }
    }

    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
