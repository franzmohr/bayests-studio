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
    QTemporaryDir dir;

    using Errors = VarBuilderSpec::Errors;
    using Selection = VarBuilderSpec::Selection;

    // -- Every combination the form allows.
    int written = 0;
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
                            s.data = data;
                            s.columns = {0, 1, 2};
                            s.p = 2;
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
                            const QString name = QStringLiteral("%1-c%2-s%3-v%4-n%5-m%6.h5")
                                                     .arg(s.algorithm())
                                                     .arg(covar).arg(structural)
                                                     .arg(static_cast<int>(selection))
                                                     .arg(nonCentred).arg(s.minnesota);
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
    std::printf("%d combinations written and run\n", written);

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
        const arma::mat y = readDraws(file, "/data/train/y");   // (1, tt*k)
        if (std::abs(y(0, 0) - data.values(1, 2)) > 1e-12 || std::abs(y(0, 1) - data.values(1, 0)) > 1e-12)
        {
            fail("/data/train/y is not stacked by period in the chosen variable order");
        }
    }

    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
