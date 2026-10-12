// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

// The state variance explorer: a time-varying VAR on the Lütkepohl data the
// studio ships, copied with its state variance set small and large and
// estimated again. The copies carry the prior asked for and nothing of the
// posterior; held fixed, the variance comes back as the one asked for; and a
// small one leaves the path flatter than a large one. The model file itself
// is left as it was.

#include "engine/DataSet.h"
#include "engine/Examples.h"
#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"
#include "engine/StateVariance.h"
#include "engine/VarBuilder.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace studio;

namespace
{

int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

bool estimate(const QString &file)
{
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
    return ran;
}

double range(const std::vector<double> &v)
{
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return *hi - *lo;
}

double median(const std::vector<double> &sorted)
{
    return sorted[sorted.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const WorkflowExample *e1 = findExample(QStringLiteral("luetkepohl2006"));
    if (e1 == nullptr)
    {
        std::printf("FAIL  the Lütkepohl example is missing\n");
        return 1;
    }
    QTemporaryDir t;
    VarBuilderSpec s;
    s.data = prepareExampleData(e1->recipe, QString());
    s.columns = {0, 1, 2};
    s.p = 1;
    s.constant = true;
    s.minnesota = false;
    s.timeVarying = true;
    s.errors = VarBuilderSpec::Errors::Gamma;
    s.iterations = 300;
    s.burnin = 300;
    s.seed = 20261013;
    s.horizon = 0;
    const QString file = t.filePath(QStringLiteral("tvp.h5"));
    writeVarModel(s, file);

    // A constant-coefficient VAR has nothing to explore.
    {
        VarBuilderSpec c = s;
        c.timeVarying = false;
        const QString constant = t.filePath(QStringLiteral("constant.h5"));
        writeVarModel(c, constant);
        const StateVariancePrior p = readStateVariancePrior(constant);
        check(!p.available && !p.refusal.isEmpty(), "a VAR with constant coefficients is refused, with a reason");
    }

    const StateVariancePrior prior = readStateVariancePrior(file);
    check(prior.available && !prior.nonCentred && prior.coefficients == 12 && prior.periods > 0,
          "a time-varying VAR's centred prior is read: 12 coefficients");
    check(std::abs(prior.typical - s.stateRate / s.stateShape) < 1e-12,
          "... the typical state variance is the slopes' rate / shape, the constants aside");
    check(estimate(file), "the model is estimated");

    const double small = prior.typical / 100.0;
    const double large = prior.typical * 100.0;
    const QString lo = t.filePath(QStringLiteral("lo.h5"));
    const QString hi = t.filePath(QStringLiteral("hi.h5"));
    writeStateVarianceVariant(file, lo, {small, true, 200, 200});
    writeStateVarianceVariant(file, hi, {large, true, 200, 200});

    const Matrix shape = readMatrix(lo, QStringLiteral("/priors/a/shape"));
    const Matrix rate = readMatrix(lo, QStringLiteral("/priors/a/rate"));
    const Matrix original = readMatrix(file, QStringLiteral("/priors/a/rate"));
    bool scaled = shape.values.size() == 12;
    for (std::size_t i = 0; scaled && i < 12; ++i)
    {
        scaled = shape.values[i] == 1e6 &&
                 std::abs(rate.values[i] / shape.values[i] - original.values[i] / s.stateShape / 100.0) <
                     1e-12 * original.values[i];
    }
    check(scaled, "a variant's prior is the model's scaled, the constants keeping their ratio, held fixed");
    const CheckReport lr = ModelRunner::check(lo, QString());
    check(lr.ok && !lr.hasPosterior && lr.iterations == 200 && lr.burnin == 200,
          "... with no posterior and the chain length asked for");
    check(ModelRunner::check(file, QString()).hasPosterior && readMatrix(file, QStringLiteral("/priors/a/rate")).values ==
                                                               original.values,
          "... and the model file left as it was");

    check(estimate(lo) && estimate(hi), "both variants are estimated");
    const int row = 0;   // the first equation's own first lag
    const StatePathSummary a = summariseStatePath(lo, row, prior.coefficients);
    const StatePathSummary b = summariseStatePath(hi, row, prior.coefficients);
    const StatePathSummary own = summariseStatePath(file, row, prior.coefficients);
    check(a.draws == 200 && own.draws == 300 && a.median.size() == static_cast<std::size_t>(prior.periods),
          "the paths are summarised per period over the kept draws");
    check(!a.stateSd.empty() && std::abs(median(a.stateSd) / std::sqrt(small) - 1.0) < 0.05 &&
              std::abs(median(b.stateSd) / std::sqrt(large) - 1.0) < 0.05,
          "held fixed, the state standard deviation comes back as the one asked for");
    std::printf("      path ranges: small %.4g, model %.4g, large %.4g\n", range(a.median), range(own.median),
                range(b.median));
    check(range(a.median) < range(b.median), "a small state variance leaves the path flatter than a large one");

    // The non-centred prior is scaled through omega_v and cannot be held.
    {
        VarBuilderSpec n = s;
        n.nonCentred = true;
        const QString nc = t.filePath(QStringLiteral("nc.h5"));
        writeVarModel(n, nc);
        const StateVariancePrior p = readStateVariancePrior(nc);
        check(p.available && p.nonCentred && std::abs(p.typical - n.omegaV) < 1e-15,
              "a non-centred prior is read through omega_v");
        const QString ncv = t.filePath(QStringLiteral("nc-variant.h5"));
        writeStateVarianceVariant(nc, ncv, {p.typical * 10.0, false, 0, -1});
        const Matrix ov = readMatrix(ncv, QStringLiteral("/priors/a/omega_v"));
        check(std::abs(ov.values.front() - n.omegaV * 10.0) < 1e-15 && ModelRunner::check(ncv, QString()).ok,
              "... and scaled there, the file still one bayests accepts");
    }

    std::printf("%s\n", failures == 0 ? "PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
