// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

// The analysis of the factor and unobserved components models, on files
// written here with draws chosen by hand: a DFM, a DFM whose loadings move, a
// FAVAR and two UC models. Each number the views show -- factor paths,
// loadings, the variance the factors explain, impulse responses, trend and
// cycle -- is checked against the same arithmetic done here. The samplers are
// BayesTS's to test; this tests what the studio makes of their draws.

#include "engine/FactorAnalysis.h"
#include "engine/TrendCycle.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <highfive/highfive.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace studio;

namespace
{

int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

bool near(double a, double b, double tol = 1e-10)
{
    return std::abs(a - b) <= tol * std::max(1.0, std::abs(b));
}

using Rows = std::vector<std::vector<double>>;

/// A draws dataset: one row per quantity, one column per draw.
void writeDraws(HighFive::File &h5, const std::string &path, const Rows &rows)
{
    h5.createDataSet(path, rows);
}

void writeModel(HighFive::File &h5, const std::string &algorithm, int k, int p, int factors, int observed = 0)
{
    HighFive::Group model = h5.createGroup("/model");
    model.createAttribute("algorithm", algorithm);
    model.createAttribute("k", k);
    model.createAttribute("p", p);
    model.createAttribute("n_factors", factors);
    if (observed > 0)
    {
        model.createAttribute("n_obs_factors", observed);
    }
}

/// y stacked by period, (1, tt*k), from `series[i][t]`.
void writePanel(HighFive::File &h5, const Rows &series)
{
    std::vector<double> stacked;
    for (std::size_t t = 0; t < series[0].size(); ++t)
    {
        for (const auto &s : series)
        {
            stacked.push_back(s[t]);
        }
    }
    h5.createDataSet("/data/train/y", Rows{stacked});
}

double variance(const std::vector<double> &x)
{
    double mean = 0;
    for (double v : x) mean += v;
    mean /= static_cast<double>(x.size());
    double ss = 0;
    for (double v : x) ss += (v - mean) * (v - mean);
    return ss / static_cast<double>(x.size() - 1);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;

    // -- A DFM: three series, one factor, p = 1, four periods, two draws.
    const Rows series = {{1.0, 2.0, 0.5, -1.0}, {0.3, 1.1, 0.9, -0.2}, {2.0, 1.0, -1.0, 0.4}};
    const Rows factorDraws = {{0.8, 1.2}, {1.9, 2.1}, {0.4, 0.6}, {-1.1, -0.9}};   // period-major, n = 1
    const double lam[2][3] = {{1.0, 0.5, -0.4}, {1.0, 0.7, -0.6}};                 // per draw, k x 1
    const double aDraws[2] = {0.5, 0.9};
    const double vPrecision[2] = {4.0, 1.0};
    const QString dfm = dir.filePath(QStringLiteral("dfm.h5"));
    {
        HighFive::File h5(dfm.toStdString(), HighFive::File::Truncate);
        writeModel(h5, "DfmNormalGamma", 3, 1, 1);
        writePanel(h5, series);
        writeDraws(h5, "/posterior/factors/coeffs", factorDraws);
        writeDraws(h5, "/posterior/lambda/coeffs",
                   {{lam[0][0], lam[1][0]}, {lam[0][1], lam[1][1]}, {lam[0][2], lam[1][2]}});
        writeDraws(h5, "/posterior/a/coeffs", {{aDraws[0], aDraws[1]}});
        writeDraws(h5, "/posterior/v_sigma_inv/coeffs", {{vPrecision[0], vPrecision[1]}});
        writeDraws(h5, "/posterior/u_sigma_inv/coeffs", {{1, 1}, {1, 1}, {1, 1}});
    }
    {
        const FactorSupport s = factorSupport(dfm);
        check(s.ok && s.k == 3 && s.factors == 1 && s.periods == 4 && s.draws == 2 && !s.favar && s.responses,
              "DFM: three series, one factor, four periods, two draws, with responses");
        check(s.series == QStringList({QStringLiteral("Series 1"), QStringLiteral("Series 2"), QStringLiteral("Series 3")}) &&
                  s.time.size() == 4 && s.time[0] == 1.0 && s.time[3] == 4.0,
              "... unnamed series and an undated sample numbered from 1");

        const auto paths = factorPaths(dfm);
        check(paths.size() == 1 && near(paths[0].median[1], 2.0) && near(paths[0].lo90[1], 1.9 + 0.05 * 0.2) &&
                  near(paths[0].hi90[3], -1.1 + 0.95 * 0.2),
              "... the factor's path: median and 90% bounds of each period, interpolated");

        const LoadingTable l = loadingTable(dfm);
        check(near(l.median(1, 0), 0.6) && near(l.lower(2, 0), -0.6 + 0.05 * 0.2) && near(l.upper(0, 0), 1.0),
              "... the loadings' median and 90% bounds");

        // R^2 per draw: 1 - var(x - lambda f) / var(x), then the median of two.
        const ExplainedShare share = explainedShare(dfm);
        double expected = 0;
        for (int d = 0; d < 2; ++d)
        {
            std::vector<double> resid;
            for (int t = 0; t < 4; ++t)
            {
                resid.push_back(series[1][t] - lam[d][1] * factorDraws[t][d]);
            }
            expected += 0.5 * (1.0 - variance(resid) / variance(series[1]));
        }
        check(share.median.size() == 3 && near(share.median[1], expected),
              "... the share of series 2's variance the factor explains, by hand");

        // A one standard deviation factor shock: lambda * a^h / sqrt(precision).
        const auto r = factorResponses(dfm, 0, 3);
        bool responses = r.size() == 2;
        for (int d = 0; d < 2 && responses; ++d)
        {
            for (int h = 0; h <= 3; ++h)
            {
                for (int i = 0; i < 3; ++i)
                {
                    responses = responses && near(r[d](h, i), lam[d][i] * std::pow(aDraws[d], h) / std::sqrt(vPrecision[d]));
                }
            }
        }
        check(responses && r[0].n_cols == 3, "... impulse responses of every series, by hand at every horizon");
    }

    // -- A DFM whose loadings move: the loadings of a chosen period.
    const QString tvp = dir.filePath(QStringLiteral("dfmtvp.h5"));
    {
        HighFive::File h5(tvp.toStdString(), HighFive::File::Truncate);
        writeModel(h5, "DfmTvpGamma", 2, 1, 1);
        writePanel(h5, {{1.0, 2.0, 3.0}, {0.5, 0.1, -0.4}});
        writeDraws(h5, "/posterior/factors/coeffs", {{1.0}, {2.0}, {3.0}});
        // k * n per period: period 0 (1.0, 0.2), period 1 (1.0, 0.4), period 2 (1.0, 0.8).
        writeDraws(h5, "/posterior/lambda/coeffs", {{1.0}, {0.2}, {1.0}, {0.4}, {1.0}, {0.8}});
        writeDraws(h5, "/posterior/a/coeffs", {{0.5}, {0.6}, {0.7}});
        writeDraws(h5, "/posterior/v_sigma_inv/coeffs", {{1.0}});
    }
    {
        check(near(loadingTable(tvp, 0).median(1, 0), 0.2) && near(loadingTable(tvp, 1).median(1, 0), 0.4) &&
                  near(loadingTable(tvp).median(1, 0), 0.8),
              "DfmTvpGamma: the loadings of the period asked for, the last by default");
        const auto r = factorResponses(tvp, 0, 2, 1);
        check(near(r[0](2, 1), 0.4 * 0.6 * 0.6), "... and its responses from that period's loadings and transition");
        const ExplainedShare share = explainedShare(tvp);
        const std::vector<double> x = {0.5, 0.1, -0.4};
        const std::vector<double> resid = {0.5 - 0.2 * 1.0, 0.1 - 0.4 * 2.0, -0.4 - 0.8 * 3.0};
        check(near(share.median[1], 1.0 - variance(resid) / variance(x)),
              "... and the share explained with each period's own loadings");
    }

    // -- A FAVAR: two panel series, one unobserved and one observed factor.
    const QString favar = dir.filePath(QStringLiteral("favar.h5"));
    {
        HighFive::File h5(favar.toStdString(), HighFive::File::Truncate);
        writeModel(h5, "FavarNormalWishart", 2, 1, 1, 1);
        writePanel(h5, {{1.0, 0.0, -1.0}, {0.5, 0.5, 0.2}});
        h5.createDataSet("/data/train/f_obs", Rows{{0.1, 0.2, 0.3}}).createAttribute("variables", std::vector<std::string>{"rate"});
        writeDraws(h5, "/posterior/factors/coeffs", {{1.0}, {0.0}, {-1.0}});
        // k x ns, column by column: [[1, 0.5], [0.3, 2]].
        writeDraws(h5, "/posterior/lambda/coeffs", {{1.0}, {0.3}, {0.5}, {2.0}});
        // A = [[0.5, 0.1], [0.2, 0.4]], column by column.
        writeDraws(h5, "/posterior/a/coeffs", {{0.5}, {0.2}, {0.1}, {0.4}});
        // Q = [[1, 0.5], [0.5, 2]]; its precision, column by column.
        const double det = 1.0 * 2.0 - 0.25;
        writeDraws(h5, "/posterior/v_sigma_inv/coeffs", {{2.0 / det}, {-0.5 / det}, {-0.5 / det}, {1.0 / det}});
    }
    {
        const FactorSupport s = factorSupport(favar);
        check(s.ok && s.favar && s.observedFactors == 1 && s.state == QStringList({QStringLiteral("Factor 1"), QStringLiteral("rate")}),
              "FAVAR: the state is the unobserved factor, then the observed one by name");
        // P = chol(Q) lower = [[1, 0], [0.5, sqrt(1.75)]]; shock 1, the observed factor's.
        const double p11 = std::sqrt(1.75);
        const arma::mat A = {{0.5, 0.1}, {0.2, 0.4}};
        const arma::mat L = {{1.0, 0.5}, {0.3, 2.0}};
        const arma::vec impact = {0.0, p11};
        const auto r = factorResponses(favar, 1, 2);
        bool ok = r.size() == 1 && r[0].n_cols == 3;
        arma::vec state = impact;
        for (int h = 0; h <= 2 && ok; ++h)
        {
            const arma::vec panel = L * state;
            ok = near(r[0](h, 0), panel(0)) && near(r[0](h, 1), panel(1)) && near(r[0](h, 2), state(1));
            state = A * state;
        }
        check(ok, "... a shock to the observed factor, ordered last: the panel through the loadings, the factor itself");
        const auto r0 = factorResponses(favar, 0, 0);
        check(near(r0[0](0, 2), 0.5), "... and the first shock moves the observed factor on impact by Q's Choleski factor");
        const ExplainedShare share = explainedShare(favar);
        // Series 1: common = 1 * f + 0.5 * rate.
        const std::vector<double> x = {1.0, 0.0, -1.0};
        const std::vector<double> resid = {1.0 - (1.0 + 0.05), 0.0 - (0.0 + 0.1), -1.0 - (-1.0 + 0.15)};
        check(near(share.median[0], 1.0 - variance(resid) / variance(x)),
              "... and the share explained counts the observed factor in the common component");
    }

    // -- Two UC models: the cycle is the data less the trend, or drawn.
    const QString uc = dir.filePath(QStringLiteral("uc.h5"));
    {
        HighFive::File h5(uc.toStdString(), HighFive::File::Truncate);
        writeModel(h5, "UcNormalGamma", 2, 1, 0);
        // One row per series, (k, tt), as the Uc* models may store it.
        h5.createDataSet("/data/train/y", Rows{{10.0, 11.0, 13.0}, {5.0, 4.0, 6.0}})
            .createAttribute("tsp", std::vector<double>{2000.0, 2000.5, 4.0});
        // Stacked by period, k = 2, three draws.
        writeDraws(h5, "/posterior/trend/coeffs",
                   {{9.0, 10.0, 11.0}, {5.0, 5.0, 5.0}, {10.5, 11.0, 11.5}, {4.0, 4.5, 5.0}, {12.0, 12.5, 13.0}, {5.5, 5.5, 5.5}});
    }
    {
        const TrendCycleSupport s = trendCycleSupport(uc);
        check(s.ok && s.k == 2 && s.periods == 3 && s.draws == 3 && !s.irregular && near(s.time[2], 2000.5),
              "UC: two series, three dated quarters, three draws, no irregular component");
        const auto tc = trendCycle(uc);
        check(tc.size() == 2 && near(tc[0].trend.median[0], 10.0) && near(tc[0].cycle.median[0], 0.0) &&
                  near(tc[1].cycle.median[1], 4.0 - 4.5) && near(tc[0].cycle.lo90[2], 13.0 - (13.0 - 0.05)) &&
                  tc[1].data == std::vector<double>({5.0, 4.0, 6.0}),
              "... the trend's median and the cycle as the data less each draw's trend");
    }
    const QString ucIrregular = dir.filePath(QStringLiteral("uc-irregular.h5"));
    {
        HighFive::File h5(ucIrregular.toStdString(), HighFive::File::Truncate);
        writeModel(h5, "UcNormalWishart", 1, 1, 0);
        h5.createDataSet("/data/train/y", Rows{{1.0, 2.0}});
        writeDraws(h5, "/posterior/trend/coeffs", {{0.5}, {1.5}});
        writeDraws(h5, "/posterior/cycle/coeffs", {{0.2}, {0.3}});
    }
    {
        const auto tc = trendCycle(ucIrregular);
        check(trendCycleSupport(ucIrregular).irregular && near(tc[0].cycle.median[0], 0.2) && near(tc[0].cycle.median[1], 0.3),
              "UC with an irregular component: the cycle drawn, not the data less the trend");
    }

    // -- Each refuses the other's models.
    check(!factorSupport(uc).ok && !trendCycleSupport(dfm).ok, "a UC model is not a factor model, nor a DFM a UC model");

    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
