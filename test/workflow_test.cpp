// The assistant's workflows: steps that tick themselves off as the project
// fills, the specification it keeps, the variants it makes, and the methods
// paragraph it drafts.
//
//   studio_workflow_test <reference dir>

#include "engine/CoefficientSummary.h"
#include "engine/DataSet.h"
#include "engine/Examples.h"
#include "engine/HistoricalDecomposition.h"
#include "engine/Structural.h"
#include "engine/Extensions.h"
#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"
#include "engine/Multipliers.h"
#include "engine/Workflow.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

using namespace studio;

namespace
{

int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

int indexOf(const Workflow &w, const QString &id)
{
    for (std::size_t i = 0; i < w.steps.size(); ++i)
    {
        if (w.steps[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    // -- The thesis workflow advances as the project fills.
    const Workflow *thesis = findWorkflow(QStringLiteral("thesis"));
    const Workflow *race = findWorkflow(QStringLiteral("horserace"));
    check(thesis && race && workflows().size() == 2, "two workflows: the master thesis and the horse race");
    ProjectState st;
    st.folder = QDir::tempPath();
    check(currentStep(*thesis, st) == 0, "an empty project starts at the research question");
    st.hasData = true;
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("inspect")),
          "with data, the next step is a judgement: looking at it");
    st.confirmed << QStringLiteral("inspect");
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("baseline")), "confirmed, on to the baseline");
    ModelFacts m;
    m.file = QDir(st.folder).filePath(QStringLiteral("baseline.h5"));
    m.ok = true;
    st.models.push_back(m);
    st.baselineModels << QStringLiteral("baseline.h5");
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("estimate")), "a model, then its estimation");
    st.models[0].estimated = true;
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("convergence")),
          "estimated, then convergence -- the optional lag comparison not blocking");
    st.confirmed << QStringLiteral("convergence");
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("identify")), "then the identification");
    st.models[0].sign = true;
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("responses")),
          "a sign identification in a file ticks the identification off");
    st.confirmed << QStringLiteral("responses");
    st.events << QStringLiteral("exported");
    check(currentStep(*thesis, st) == static_cast<int>(thesis->steps.size()) - 1, "everything done: the last step");

    // -- The horse race advances with its files.
    ProjectState hr;
    hr.hasData = true;
    check(currentStep(*race, hr) == indexOf(*race, QStringLiteral("design")), "the race starts with its design");
    hr.horseRace = true;
    hr.horseRaceModels = 12;
    hr.horseRaceRun = 5;
    check(currentStep(*race, hr) == indexOf(*race, QStringLiteral("run")), "a design half run is still running");
    hr.horseRaceRun = 12;
    check(currentStep(*race, hr) == indexOf(*race, QStringLiteral("results")), "run, then the league table");

    // -- The worked examples: their data prepared from the reproductions, and
    // the examples the studio can implement accepted by the builder.
    {
        const QString root = defaultReproductionsFolder();
        check(examplesFor(QStringLiteral("thesis")).size() >= 2 && examplesFor(QStringLiteral("horserace")).size() >= 1,
              "worked examples for both workflows");
        if (QDir(root).exists())
        {
            const WorkflowExample *arw = findExample(QStringLiteral("arw2018"));
            const DataSet d = prepareExampleData(arw->recipe, root);
            check(d.length() == 224 && d.series() == 5 && d.periods.front() == QStringLiteral("1955 Q1") &&
                      d.periods.back() == QStringLiteral("2010 Q4") && d.names.front() == QStringLiteral("tfp"),
                  "Arias et al.: 224 quarters of five series, 1955Q1 to 2010Q4");
            check(std::abs(d.values(0, 0) - 100 * 0.21720722) < 1e-9, "... multiplied by 100");
            const WorkflowExample *lp = findExample(QStringLiteral("lp2022race"));
            const DataSet l = prepareExampleData(lp->recipe, root);
            check(l.periods.front() == QStringLiteral("1988-12") && l.series() == 4 && l.values.is_finite(),
                  "Lenza and Primiceri: four monthly series from 1988-12, complete");
            QTemporaryDir t;
            writeDataSet(d, t.filePath(QStringLiteral("arw.csv")));
            const DataSet back = readDelimited(t.filePath(QStringLiteral("arw.csv")));
            check(back.names == d.names && back.periods == d.periods && arma::approx_equal(back.values, d.values, "absdiff", 1e-12),
                  "a prepared data set written and read back");
        }
        else
        {
            std::printf("skip  example data: no reproductions in %s\n", root.toStdString().c_str());
        }
    }

    // -- LÃ¼tkepohl's E1, which the studio ships: the recipe's log differences
    // are bvartools' diff(log(e1)) * 100, and the example estimated as it is
    // specified gives the numbers of the bvartools introduction.
    {
        const WorkflowExample *e1 = findExample(QStringLiteral("luetkepohl2006"));
        check(e1 && isBuiltinData(e1->recipe) && examplesFor(QStringLiteral("thesis")).front() == e1,
              "LÃ¼tkepohl (2006): the first thesis example, on data the studio ships");
        bool papers = !e1->reproduction;
        for (const WorkflowExample *e : examplesFor(QStringLiteral("thesis")))
        {
            papers = papers && (e == e1 || e->reproduction);
        }
        check(papers, "... the one introductory example, every other a paper reproduced");
        const DataSet d = prepareExampleData(e1->recipe, QString());
        const DataSet ref = readDelimited(dir + "/e1.csv");   // bvartools' diff(log(e1)) * 100, to 1982Q4
        check(d.length() == 75 && d.series() == 3 && d.periods.front() == QStringLiteral("1960 Q2") &&
                  d.periods.back() == QStringLiteral("1978 Q4") && d.names == ref.names &&
                  arma::approx_equal(d.values, ref.values.rows(0, 74), "absdiff", 1e-9),
              "... 75 quarters of 100 x log differences, 1960Q2 to 1978Q4, as bvartools has them");

        QTemporaryDir t;
        DataRecipe bad;
        bad.file = t.filePath(QStringLiteral("zero.csv"));
        bad.dateColumn = QStringLiteral("date");
        bad.columns = {{QStringLiteral("x"), QStringLiteral("x")}};
        bad.transform = DataRecipe::Transform::LogDifference;
        {
            QFile f(bad.file);
            f.open(QIODevice::WriteOnly);
            f.write("date,x\n2000-01-01,1\n2000-02-01,0\n");
        }
        bool refused = false;
        try
        {
            prepareExampleData(bad, QString());
        }
        catch (const std::exception &)
        {
            refused = true;
        }
        check(refused, "... and a log difference of a value not positive refused");

        // The baseline as its dialog fills it in.
        VarBuilderSpec s;
        s.data = d;
        s.columns = {0, 1, 2};
        s.p = e1->p;
        s.constant = true;
        s.minnesota = false;
        s.coefPrecision = e1->coefPrecision;
        s.detPrecision = e1->detPrecision;
        s.wishartScale = e1->wishartScale;
        s.wishartDfExtra = e1->wishartDf - 3;
        s.iterations = e1->iterations;
        s.burnin = e1->burnin;
        s.seed = e1->seed;
        s.horizon = e1->forecastHorizon;
        const QString file = t.filePath(QStringLiteral("e1.h5"));
        check(summarise(s).refusal.isEmpty(), "... its baseline a model the builder accepts");
        writeVarModel(s, file);
        ModelRunner runner;
        QEventLoop loop;
        bool ran = false;
        QObject::connect(&runner, &ModelRunner::runFinished, &loop, [&](bool ok, bool, const QString &error) {
            ran = ok;
            if (!ok)
            {
                std::printf("      %s\n", error.toStdString().c_str());
            }
            loop.quit();
        });
        runner.start(file, QString(), {Stage::Coefficients, Stage::Forecast, Stage::LogLikelihood});
        loop.exec();
        check(ran && ModelRunner::check(file, QString()).horizon == 10, "... and estimated, forecasting ten quarters as the vignette");
        if (ran)
        {
            // bvartools: income.l1 and income.l2 in the cons equation 0.228 and
            // 0.357 (posterior sd 0.115 and 0.112); the oir of cons to income
            // 0.494 on impact; income's share of cons's forecast error variance
            // 0.273 on impact and 0.344 at eight quarters.
            const CoefficientSummary sum = coefficientSummary(file);
            const CoefficientBlock &a = sum.blocks.front();
            const auto mean = [&](int eq, const QString &col) {
                const qsizetype c = a.columns.indexOf(col);
                // Panels run regressor by regressor, the equations within each.
                return c < 0 ? NAN
                             : arma::mean(a.panels.at(static_cast<std::size_t>(c * sum.equations.size() + eq)).draws);
            };
            check(std::abs(mean(2, QStringLiteral("income(-1)")) - 0.228) < 0.02 &&
                      std::abs(mean(2, QStringLiteral("income(-2)")) - 0.357) < 0.02,
                  "... the income lags in the consumption equation as bvartools has them");
            ResponseRequest r;
            r.type = ResponseType::Oir;
            r.horizon = 8;
            const auto oir = impulseResponses(file, r, 1);
            arma::vec impact(oir.size());
            for (std::size_t i = 0; i < oir.size(); ++i)
            {
                impact(i) = oir[i](0, 2);
            }
            const auto fevd = varianceDecomposition(file, r, 2);
            arma::vec share0(fevd.size()), share8(fevd.size());
            for (std::size_t i = 0; i < fevd.size(); ++i)
            {
                share0(i) = fevd[i](0, 1);
                share8(i) = fevd[i](8, 1);
            }
            std::printf("      oir impact %.3f, fevd income share %.3f and %.3f\n", arma::median(impact),
                        arma::mean(share0), arma::mean(share8));
            check(std::abs(arma::median(impact) - 0.494) < 0.03 && std::abs(arma::mean(share0) - 0.273) < 0.03 &&
                      std::abs(arma::mean(share8) - 0.344) < 0.03,
                  "... the response of cons to income and its variance share as bvartools has them");
        }
    }

    // -- Pinchetti (2026), from the reproduction's files: the semicolon file
    // with its year and month columns, the paper's prior against the scales
    // the bvartools reproduction prints, the surprise equations held at zero,
    // and the two shocks identified by a rotation of the surprises alone, with
    // thresholds -- and the responses where that reproduction has them.
    if (const QString root = defaultReproductionsFolder(); QDir(root).exists(QStringLiteral("2026-pinchetti")))
    {
        const WorkflowExample *ex = findExample(QStringLiteral("pinchetti2026"));
        check(ex && ex->supported && ex->nIid == 2 && ex->signRotated == 2 && ex->paperPrior,
              "Pinchetti (2026): a guided example, two surprises and two rotated shocks");
        const DataSet d = prepareExampleData(ex->recipe, root);
        int surpriseMonths = 0;
        for (arma::uword t = 0; t < d.values.n_rows; ++t)
        {
            surpriseMonths += (d.values(t, 0) != 0.0 || d.values(t, 1) != 0.0) ? 1 : 0;
        }
        check(d.length() == 408 && d.series() == 7 && d.periods.front() == QStringLiteral("1986-01") &&
                  d.periods.back() == QStringLiteral("2019-12") && surpriseMonths == 30,
              "... 408 months of seven series from the semicolon file, 30 of them with a surprise");

        // The baseline as the dialog builds it with the paper's prior chosen;
        // fewer draws than the paper's, for the test's time.
        VarBuilderSpec s;
        s.data = d;
        s.columns = {0, 1, 2, 3, 4, 5, 6};
        s.p = ex->p;
        s.minnesota = true;
        s.kappa1 = ex->paperPrior->kappa1;
        s.kappa2 = ex->paperPrior->kappa2;
        s.lagDecay = ex->paperPrior->lagDecay;
        s.minnesotaScale = VarBuilderSpec::MinnesotaScale::Autoregressions;
        s.minnesotaDetVariance = ex->paperPrior->deterministicVariance;
        s.wishartScaleFromResiduals = true;
        for (const int c : s.columns)
        {
            s.ownLagMean.push_back(ex->paperPrior->randomWalk.contains(d.names.value(c)) ? 1.0 : 0.0);
        }
        s.wishartDfExtra = ex->wishartDf - 7;
        s.nIid = ex->nIid;
        s.iterations = 400;
        s.burnin = 400;
        s.seed = ex->seed;
        QTemporaryDir t;
        const QString file = t.filePath(QStringLiteral("pinchetti.h5"));
        check(summarise(s).refusal.isEmpty(), "... its baseline a model the builder accepts");
        writeVarModel(s, file);
        // bvartools' reproduction: round(sigma_i, 3), from AR(12)s with a constant.
        const double reference[] = {0.419, 1.994, 21.181, 7.654, 0.218, 0.549, 0.227};
        const Matrix scale = readMatrix(file, QStringLiteral("/priors/u_sigma/scale"));
        double worstScale = 0.0;
        for (std::size_t i = 0; i < 7; ++i)
        {
            worstScale = std::max(worstScale, std::abs(std::sqrt(scale.at(i, i)) - reference[i]));
        }
        // The prior variance of equation 3 (CPI) on WTI's twelfth lag:
        // (0.1 * 12^-2 * sigma_CPI / sigma_WTI)^2 -- coefficient q*k + i.
        const Matrix vInv = readMatrix(file, QStringLiteral("/priors/a/v_inv"));
        const std::size_t pos = (11 * 7 + 3) * 7 + 4;
        const double expected = std::pow(0.1 / 144.0 * std::sqrt(scale.at(4, 4) / scale.at(3, 3)), 2.0);
        check(worstScale < 6e-4 && std::abs(1.0 / vInv.at(pos, pos) / expected - 1.0) < 1e-9,
              "... the paper's prior: the AR(12) scales as bvartools prints them, the variance falling as l^-4");

        ModelRunner runner;
        QEventLoop loop;
        bool ran = false;
        QObject::connect(&runner, &ModelRunner::runFinished, &loop, [&](bool ok, bool, const QString &error) {
            ran = ok;
            if (!ok)
            {
                std::printf("      %s\n", error.toStdString().c_str());
            }
            loop.quit();
        });
        runner.start(file, QString(), {Stage::Coefficients});
        loop.exec();
        const Matrix a = ran ? readMatrix(file, QStringLiteral("/posterior/a/coeffs")) : Matrix{};
        double surpriseCoefficient = ran ? 0.0 : 1.0;
        for (std::size_t q = 0; q < a.rows; ++q)
        {
            for (std::size_t dr = 0; q % 7 < 2 && dr < a.cols; ++dr)
            {
                surpriseCoefficient = std::max(surpriseCoefficient, std::abs(a.at(q, dr)));
            }
        }
        check(ran && a.rows == 7 * 85 && surpriseCoefficient == 0.0,
              "... estimated, the surprise equations' coefficients exactly zero in every draw");

        if (ran)
        {
            SignIdentification spec;
            spec.rotated = ex->signRotated;
            for (const ExampleRestriction &r : ex->restrictions)
            {
                spec.restrictions.push_back({static_cast<int>(d.names.indexOf(r.impulse)),
                                             static_cast<int>(d.names.indexOf(r.response)), r.sign, r.horizon,
                                             r.threshold});
            }
            const SignIdentification id = identifySigns(file, spec, 12345);
            const auto stored = readSignIdentification(file);
            const Matrix q = readMatrix(file, QStringLiteral("/posterior/q/coeffs"));
            bool block = q.rows == 49;
            for (std::size_t dr = 0; dr < q.cols && block; ++dr)
            {
                // Column-major k x k: the identity outside the leading 2 x 2.
                for (std::size_t c = 0; c < 7; ++c)
                {
                    for (std::size_t r = 0; r < 7; ++r)
                    {
                        if (r >= 2 || c >= 2)
                        {
                            block = block && q.at(c * 7 + r, dr) == (r == c ? 1.0 : 0.0);
                        }
                    }
                }
            }
            check(id.accepted == id.draws && block && stored && stored->rotated == 2 &&
                      stored->restrictions.front().threshold == 0.01,
                  "... every draw identified by a rotation of the two surprise shocks alone, thresholds kept");

            ResponseRequest r;
            r.type = ResponseType::Sign;
            r.horizon = 36;
            const auto median = [](const std::vector<arma::mat> &draws, arma::uword h, arma::uword v) {
                arma::vec x(draws.size());
                for (std::size_t i = 0; i < draws.size(); ++i)
                {
                    x(i) = draws[i](h, v);
                }
                return arma::median(x);
            };
            const auto macro = impulseResponses(file, r, 0);
            const auto energy = impulseResponses(file, r, 1);
            double energyCpi = -1.0;
            for (arma::uword h = 0; h <= 12; ++h)
            {
                energyCpi = std::max(energyCpi, median(energy, h, 4));
            }
            // bvartools' impact medians, unscaled: GPR 8.02 and 10.67, WTI -1.55 and 1.46.
            std::printf("      impact GPR %.2f / %.2f, WTI %.2f / %.2f; energy CPI peak in the first year %.3f\n",
                        median(macro, 0, 2), median(energy, 0, 2), median(macro, 0, 3), median(energy, 0, 3), energyCpi);
            check(std::abs(median(macro, 0, 2) / 8.02 - 1.0) < 0.25 && std::abs(median(energy, 0, 2) / 10.67 - 1.0) < 0.25 &&
                      median(macro, 0, 3) < 0.0 && median(energy, 0, 3) > 0.0 && energyCpi > 0.0,
                  "... the responses as the bvartools reproduction has them: oil tells the shocks apart, the energy "
                  "one raises prices");
        }
    }
    else
    {
        std::printf("skip  Pinchetti (2026): no reproductions in %s\n", defaultReproductionsFolder().toStdString().c_str());
    }

    // -- JarociÅ„ski and Karadi (2020): the surprises missing before 1990 kept,
    // written as constraints and drawn every sweep, the scales from AR(12)s
    // with the gaps dropped as the replication code does, and Table 2 and
    // Figure 4 where the bvartools reproduction has them.
    if (const QString root = defaultReproductionsFolder(); QDir(root).exists(QStringLiteral("2020-jarocinski karadi")))
    {
        const WorkflowExample *ex = findExample(QStringLiteral("jk2020"));
        check(ex && ex->supported && ex->recipe.keepMissing && ex->nIid == 2 && ex->signRotated == 2,
              "JarociÅ„ski and Karadi (2020): a guided example, its missing surprises kept");
        const DataSet d = prepareExampleData(ex->recipe, root);
        const arma::uword missingFf4 = arma::accu(d.values.col(0) != d.values.col(0));
        const arma::uword missingSp = arma::accu(d.values.col(1) != d.values.col(1));
        check(d.length() == 395 && d.periods.front() == QStringLiteral("1984-02") && missingFf4 == 72 &&
                  missingSp == 73 && d.values.cols(2, 6).is_finite(),
              "... 395 months, 72 and 73 surprises missing, as bvartools counts them");
        {
            // A data set written and read back keeps its gaps as gaps.
            QTemporaryDir t;
            writeDataSet(d, t.filePath(QStringLiteral("jk.csv")));
            const DataSet back = readDelimited(t.filePath(QStringLiteral("jk.csv")));
            check(arma::accu(back.values.col(1) != back.values.col(1)) == 73 &&
                      arma::approx_equal(back.values.cols(2, 6), d.values.cols(2, 6), "absdiff", 1e-12),
                  "... and written and read back with them");
        }

        VarBuilderSpec s;
        s.data = d;
        s.columns = {0, 1, 2, 3, 4, 5, 6};
        s.p = ex->p;
        s.minnesota = true;
        s.kappa1 = ex->paperPrior->kappa1;
        s.kappa2 = ex->paperPrior->kappa2;
        s.lagDecay = ex->paperPrior->lagDecay;
        s.minnesotaScale = VarBuilderSpec::MinnesotaScale::Autoregressions;
        s.minnesotaDetVariance = ex->paperPrior->deterministicVariance;
        s.wishartScaleFromResiduals = true;
        for (const int c : s.columns)
        {
            s.ownLagMean.push_back(ex->paperPrior->randomWalk.contains(d.names.value(c)) ? 1.0 : 0.0);
        }
        s.wishartDfExtra = ex->wishartDf - 7;
        s.nIid = ex->nIid;
        s.iterations = 400;
        s.burnin = 400;
        s.seed = ex->seed;
        const VarBuilderSummary summary = summarise(s);
        check(summary.refusal.isEmpty() && summary.periods == 383 &&
                  summary.notes.join(QLatin1Char(' ')).contains(QStringLiteral("121 value")),
              "... 383 periods, 121 surprises to draw: a model the builder accepts");
        QTemporaryDir t;
        const QString file = t.filePath(QStringLiteral("jk.h5"));
        writeVarModel(s, file);
        const Matrix value = readMatrix(file, QStringLiteral("/data/train/constraints/value"));
        // bvartools' reproduction: round(sigma_i, 4).
        const double reference[] = {0.0517, 0.4839, 0.1970, 3.4035, 0.5266, 0.0996, 0.2351};
        const Matrix scale = readMatrix(file, QStringLiteral("/priors/u_sigma/scale"));
        double worstScale = 0.0;
        for (std::size_t i = 0; i < 7; ++i)
        {
            worstScale = std::max(worstScale, std::abs(std::sqrt(scale.at(i, i)) - reference[i]));
        }
        check(value.rows * value.cols == 2560 && worstScale < 6e-5,
              "... 2560 observed entries as constraints, and the AR(12) scales as bvartools prints them");

        ModelRunner runner;
        QEventLoop loop;
        bool ran = false;
        QObject::connect(&runner, &ModelRunner::runFinished, &loop, [&](bool ok, bool, const QString &error) {
            ran = ok;
            if (!ok)
            {
                std::printf("      %s\n", error.toStdString().c_str());
            }
            loop.quit();
        });
        runner.start(file, QString(), {Stage::Coefficients});
        loop.exec();
        check(ran, "... estimated");
        if (ran)
        {
            // The completion: observed entries as observed, the missing fed
            // funds surprises of 1985-02 to 1990-01 spread like the observed.
            const Matrix completed = readMatrix(file, QStringLiteral("/posterior/y/coeffs"));
            double worstObserved = 0.0;
            arma::vec spread(60);
            for (std::size_t t = 0; t < 383; ++t)
            {
                const arma::uword row = 12 + t;
                for (std::size_t i = 0; i < 7; ++i)
                {
                    const double v = d.values(row, i);
                    if (std::isfinite(v))
                    {
                        worstObserved = std::max(worstObserved, std::abs(completed.at(t * 7 + i, 0) - v));
                    }
                }
                if (t < 60)
                {
                    arma::vec draws(completed.cols);
                    for (std::size_t dr = 0; dr < completed.cols; ++dr)
                    {
                        draws(dr) = completed.at(t * 7, dr);
                    }
                    spread(t) = arma::stddev(draws);
                }
            }
            std::printf("      a missing ff4_hf: posterior sd %.4f on average (bvartools 0.0504)\n", arma::mean(spread));
            check(completed.rows == 383 * 7 && worstObserved < 1e-10 && std::abs(arma::mean(spread) - 0.0504) < 0.01,
                  "... the panel completed every draw: the observed kept, the missing drawn with the spread bvartools has");

            SignIdentification spec;
            spec.rotated = ex->signRotated;
            for (const ExampleRestriction &r : ex->restrictions)
            {
                spec.restrictions.push_back({static_cast<int>(d.names.indexOf(r.impulse)),
                                             static_cast<int>(d.names.indexOf(r.response)), r.sign, r.horizon,
                                             r.threshold});
            }
            const SignIdentification id = identifySigns(file, spec, 12345);
            // Table 2: impact responses of the surprises, posterior means in basis points.
            ResponseRequest r;
            r.type = ResponseType::Sign;
            r.horizon = 0;
            const auto mean = [](const std::vector<arma::mat> &draws, arma::uword v) {
                double sum = 0.0;
                for (const arma::mat &m : draws)
                {
                    sum += m(0, v);
                }
                return 100.0 * sum / static_cast<double>(draws.size());
            };
            const auto mp = impulseResponses(file, r, 0);
            const auto cbi = impulseResponses(file, r, 1);
            r.type = ResponseType::Oir;
            const auto hfi = impulseResponses(file, r, 0);
            std::printf("      Table 2: %.1f %.1f | %.1f %.1f | %.1f %.1f (the paper 5 -42 | 3 28 | 6 -21)\n",
                        mean(mp, 0), mean(mp, 1), mean(cbi, 0), mean(cbi, 1), mean(hfi, 0), mean(hfi, 1));
            check(id.accepted == id.draws && std::abs(mean(mp, 0) - 5.0) < 2.0 && std::abs(mean(mp, 1) + 42.0) < 8.0 &&
                      std::abs(mean(cbi, 0) - 3.0) < 2.0 && std::abs(mean(cbi, 1) - 28.0) < 10.0 &&
                      std::abs(mean(hfi, 0) - 6.0) < 1.5 && std::abs(mean(hfi, 1) + 21.0) < 4.0,
                  "... every draw identified, and Table 2 as the paper has it");

            // Figure 4: the surprise is white noise, so its two shocks'
            // contributions are the surprise itself wherever it was observed.
            HdRequest hr;
            hr.type = HdType::Sign;
            hr.response = 0;
            const HistoricalDecomposition hd = historicalDecomposition(file, hr);
            double worstSum = 0.0;
            for (arma::uword t = 0; t < 383; ++t)
            {
                if (std::isfinite(d.values(12 + t, 0)))
                {
                    worstSum = std::max(worstSum, std::abs(hd.centre(t, 0) + hd.centre(t, 1) - d.values(12 + t, 0)));
                }
            }
            check(worstSum < 1e-9, "... Figure 4: the two shocks' contributions add up to every observed surprise");
        }
    }
    else
    {
        std::printf("skip  JarociÅ„ski and Karadi (2020): no reproductions in %s\n",
                    defaultReproductionsFolder().toStdString().c_str());
    }

    // -- Multipliers of every exogenous variable, as the multipliers view's
    // matrix asks for them: invest, with income and cons weakly exogenous.
    // STUDIO_TEST_KEEP=<folder> keeps the model, to look at.
    {
        QTemporaryDir t;
        VarBuilderSpec x;
        x.data = readDelimited(dir + "/e1.csv");
        x.columns = {0};
        x.exogenous = {1, 2};
        x.p = 2;
        x.s = 1;
        x.iterations = 1000;
        x.burnin = 200;
        x.seed = 7;
        const QString file = t.filePath(QStringLiteral("varx2.h5"));
        writeVarModel(x, file);
        ModelRunner runner;
        QEventLoop loop;
        bool ran = false;
        QObject::connect(&runner, &ModelRunner::runFinished, &loop, [&](bool ok, bool, const QString &) {
            ran = ok;
            loop.quit();
        });
        runner.start(file, QString(), {Stage::Coefficients});
        loop.exec();
        const MultiplierSupport support = ran ? multiplierSupport(file) : MultiplierSupport{};
        bool each = support.ok && support.m == 2 && support.k == 1;
        for (int j = 0; each && j < support.m; ++j)
        {
            MultiplierRequest r;
            r.impulse = j;
            r.horizon = 8;
            const auto draws = dynamicMultipliers(file, r);
            each = !draws.empty() && draws.front().n_rows == 9 && draws.front().n_cols == 1 && draws.front().is_finite();
        }
        check(ran && each, "a VARX with two exogenous variables: the multipliers of each, as the matrix shows them");
        if (const QString keep = qEnvironmentVariable("STUDIO_TEST_KEEP"); !keep.isEmpty() && ran)
        {
            QFile::remove(QDir(keep).filePath(QStringLiteral("varx2.h5")));
            QFile::copy(file, QDir(keep).filePath(QStringLiteral("varx2.h5")));
        }
    }

    // -- Every step says what to do.
    bool complete = true;
    for (const Workflow &w : workflows())
    {
        for (const WorkflowStep &s : w.steps)
        {
            complete = complete && !s.title.isEmpty() && s.guidance.size() > 100 && (s.done || !s.confirm.isEmpty());
        }
    }
    check(complete, "every step has guidance and a way to be done");

    // -- Extensions: every built-in example survives a round trip through its
    // file form; a folder of files loads, a broken one says why and gives
    // nothing, an id of a built-in replaces it, and the template loads clean.
    {
        bool roundTrip = true;
        for (const WorkflowExample &e : builtinExamples())
        {
            const QJsonObject once = exampleToJson(e);
            QStringList warnings;
            const WorkflowExample back = exampleFromJson(once, QStringLiteral("/somewhere"), &warnings);
            roundTrip = roundTrip && exampleToJson(back) == once && warnings.isEmpty() &&
                        back.candidates == e.candidates && back.benchmark == e.benchmark &&
                        back.restrictions.size() == e.restrictions.size() && back.notes == e.notes;
            if (!roundTrip)
            {
                // Both forms beside each other, to diff.
                for (const auto &[name, json] : {std::pair{"once", once}, std::pair{"back", exampleToJson(back)}})
                {
                    QFile f(QDir::temp().filePath(QStringLiteral("roundtrip-%1.json").arg(QLatin1String(name))));
                    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                    f.write(QJsonDocument(json).toJson());
                }
                std::printf("      round trip of %s: %s (both forms in %s)\n", e.id.toStdString().c_str(),
                            warnings.join(QStringLiteral("; ")).toStdString().c_str(),
                            QDir::tempPath().toStdString().c_str());
                break;
            }
        }
        check(roundTrip, "every built-in example survives its file form, race candidates by key");

        QTemporaryDir t;
        const QDir user(t.filePath(QStringLiteral("extensions")));
        QDir().mkpath(user.path());
        const QDir repro(t.filePath(QStringLiteral("reproductions")));
        QDir().mkpath(repro.filePath(QStringLiteral("2020-someone")));
        const auto write = [](const QString &path, const QByteArray &text) {
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(text);
        };
        // A race on data beside the file, with a typo, and a palette.
        write(repro.filePath(QStringLiteral("2020-someone/data.csv")),
              "DATE,a,b\n2000-01-01,1,2\n2000-02-01,2,3\n2000-03-01,3,5\n");
        write(repro.filePath(QStringLiteral("2020-someone/studio-example.json")), R"js({
  "studio-extension": 1,
  "examples": [{
    "id": "someone2020", "workflow": "horserace", "title": "Forecasting: Someone (2020)",
    "data": {"file": "data.csv", "dateColumn": "DATE", "columns": [{"source": "a"}, {"source": "b", "name": "bee"}]},
    "model": {"lags": 2, "iterations": 300, "burnin": 100, "seed": 7},
    "race": {"firstOrigin": "2000-02", "horizon": 1, "candidates": ["minnesota", "one-lag"], "benchmark": "one-lag"},
    "notes": {"design": "Two models.", "nosuchstep": "never shown"},
    "colour": "a typo"
  }],
  "palettes": [{"id": "two", "name": "Two", "colours": ["#000000", "#ff0000"]}]
})js");
        // Broken JSON, and a candidate the field does not have.
        write(user.filePath(QStringLiteral("broken.json")), "{ \"studio-extension\": 1, ");
        write(user.filePath(QStringLiteral("vec.json")), R"js({"studio-extension": 1, "examples": [{
            "id": "v", "workflow": "horserace", "title": "V",
            "race": {"candidates": ["vec"]}}]})js");
        // A built-in replaced.
        write(user.filePath(QStringLiteral("override.json")), R"js({"studio-extension": 1, "examples": [{
            "id": "arw2018", "workflow": "thesis", "title": "Optimism shocks: replaced"}]})js");

        const QStringList files = extensionFiles(user.path(), repro.path());
        check(files.size() == 4, "the user's *.json and each reproduction's studio-example.json found");
        const std::vector<ExtensionReport> r = loadExtensions(files);
        const auto report = [&](const QString &name) -> const ExtensionReport * {
            for (const ExtensionReport &x : r)
            {
                if (x.file.endsWith(name))
                {
                    return &x;
                }
            }
            return nullptr;
        };
        const ExtensionReport *broken = report(QStringLiteral("broken.json"));
        const ExtensionReport *vec = report(QStringLiteral("vec.json"));
        const ExtensionReport *someone = report(QStringLiteral("studio-example.json"));
        check(broken && broken->error.contains(QStringLiteral("not JSON")) && broken->examples.isEmpty(),
              "broken JSON is reported and gives nothing");
        check(vec && vec->error.contains(QStringLiteral("levels")),
              "a VEC in a race on growth rates is refused, saying it needs levels");
        check(someone && someone->error.isEmpty() && someone->examples == QStringList{QStringLiteral("someone2020")} &&
                  someone->palettes == QStringList{QStringLiteral("two")} && someone->warnings.size() == 2,
              "a reproduction's example and palette read, its typo and its unknown step warned about");
        const WorkflowExample *ex = findExample(QStringLiteral("someone2020"));
        check(ex && ex->benchmark == 1 && ex->candidates == std::vector<int>({0, 4}) &&
                  ex->series == QStringList({QStringLiteral("a"), QStringLiteral("bee")}),
              "its candidates by key, the benchmark among them, the series from the columns");
        const DataSet d = ex ? prepareExampleData(ex->recipe, ex->dataRoot) : DataSet{};
        check(d.series() == 2 && d.length() == 3 && d.names.back() == QStringLiteral("bee"),
              "its data read beside its file");
        const WorkflowExample *arw = findExample(QStringLiteral("arw2018"));
        check(arw && arw->title.endsWith(QStringLiteral("replaced")) && !arw->source.isEmpty(),
              "an extension with a built-in's id replaces it");
        check(extensionPalettes().size() == 1, "the palette kept for the views");

        loadExtensions({});
        check(findExample(QStringLiteral("someone2020")) == nullptr && findExample(QStringLiteral("arw2018")) &&
                  findExample(QStringLiteral("arw2018"))->source.isEmpty(),
              "loading none restores the studio's own");

        const QString templ = writeExtensionTemplate(t.filePath(QStringLiteral("template")));
        const QString asJson = t.filePath(QStringLiteral("template/template.json"));
        QFile::copy(templ, asJson);
        const std::vector<ExtensionReport> tr = loadExtensions({asJson});
        QFile readme(t.filePath(QStringLiteral("template/README.md")));
        readme.open(QIODevice::ReadOnly);
        const QString md = QString::fromUtf8(readme.readAll());
        check(tr.size() == 1 && tr[0].error.isEmpty() && tr[0].warnings.isEmpty() && tr[0].examples.size() == 1 &&
                  tr[0].palettes.size() == 1 && md.contains(QStringLiteral("`inspect`")) &&
                  md.contains(QStringLiteral("`stability`")),
              "the template loads clean, and its README lists every step id");
        loadExtensions({});
    }

    if (dir.isEmpty() || !QFile::exists(dir + "/e1.csv"))
    {
        std::printf("skip  specifications and the methods paragraph: no reference data\n");
        std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
        return failures == 0 ? 0 : 1;
    }

    // -- The specification round trip, and the variants.
    const DataSet data = readDelimited(dir + "/e1.csv");
    VarBuilderSpec b;
    b.data = data;
    b.columns = {2, 0, 1};
    b.p = 4;
    b.minnesota = true;
    b.kappa1 = 0.3;
    b.seed = 42;
    b.firstRow = 3;
    b.lastRow = data.length() - 5;
    const auto back = specFromJson(specToJson(b), data);
    check(back && back->columns == b.columns && back->p == 4 && back->minnesota && back->kappa1 == 0.3 &&
              back->seed == b.seed && back->firstRow == 3 && back->lastRow == b.lastRow,
          "a specification survives JSON, columns and sample by name and label");
    DataSet other = data;
    other.names[0] = QStringLiteral("renamed");
    check(!specFromJson(specToJson(b), other), "... and refuses a data set without its variables");
    const auto lags = lagVariants(b);
    check(lags.size() == 3 && lags[0].second.p == 3 && lags[2].second.p == 6, "lag variants p - 1, p + 1, p + 2");
    const auto robust = robustnessVariants(b);
    check(robust.size() == 4 && robust[0].second.columns == std::vector<int>({1, 0, 2}) &&
              robust[1].second.kappa1 == 0.6 && robust[2].second.kappa1 == 0.15 && robust[3].second.firstRow > b.firstRow,
          "robustness variants: reverse order, looser and tighter prior, later sample");
    bool accepted = true;
    for (const auto &v : robust)
    {
        accepted = accepted && summarise(v.second).refusal.isEmpty();
    }
    check(accepted, "... each a model the builder accepts");

    // -- The methods paragraph, from a model the builder writes and from a
    // sign identified one, and that it compiles.
    QTemporaryDir tmp;
    const QString file = tmp.filePath(QStringLiteral("baseline.h5"));
    writeVarModel(b, file);
    const QString text = methodsParagraph(file, b);
    check(text.contains(QStringLiteral("cons, invest and income")) && text.contains(QStringLiteral("Minnesota")) &&
              text.contains(QStringLiteral("quarterly")) && text.contains(QStringLiteral("seed 42")) &&
              text.contains(QStringLiteral("Cholesky")),
          "the methods paragraph names the variables, prior, frequency, seed and identification");
    const QString signText = QFile::exists(dir + "/sign.h5") ? methodsParagraph(dir + "/sign.h5", std::nullopt) : QString();
    check(signText.isEmpty() || signText.contains(QStringLiteral("sign restrictions")),
          "... and a sign identification where the file has one");
    const QString pdflatex = QStandardPaths::findExecutable(QStringLiteral("pdflatex"));
    if (!pdflatex.isEmpty())
    {
        QFile doc(tmp.filePath(QStringLiteral("doc.tex")));
        doc.open(QIODevice::WriteOnly);
        doc.write(QStringLiteral("\\documentclass{article}\\begin{document}%1\n\n%2\\end{document}\n")
                      .arg(text, signText)
                      .toUtf8());
        doc.close();
        QProcess run;
        run.setWorkingDirectory(tmp.path());
        run.start(pdflatex, {QStringLiteral("-interaction=nonstopmode"), QStringLiteral("-halt-on-error"),
                             QStringLiteral("doc.tex")});
        run.waitForFinished(120000);
        check(run.exitCode() == 0, "the methods paragraphs compile");
    }
    std::printf("%s\n", text.toStdString().c_str());
    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
