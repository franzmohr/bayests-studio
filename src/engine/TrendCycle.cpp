// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/TrendCycle.h"

#include "engine/ModelFileTree.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <mutex>
#include <stdexcept>

namespace studio
{

namespace
{

std::runtime_error failure(const QString &message)
{
    return std::runtime_error(message.toStdString());
}

TrendCycleSupport readSupport(const QString &file)
{
    TrendCycleSupport s;
    const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    const HighFive::Group model = h5.getGroup("/model");
    if (!model.hasAttribute("algorithm"))
    {
        throw failure(QObject::tr("The file names no algorithm."));
    }
    s.algorithm = QString::fromStdString(model.getAttribute("algorithm").read<std::string>());
    if (!s.algorithm.startsWith(QLatin1String("Uc")))
    {
        throw failure(QObject::tr("%1 is not an unobserved components model.").arg(s.algorithm));
    }
    if (!h5.exist("/posterior/trend/coeffs"))
    {
        throw failure(QObject::tr("Estimate the model first: it has no posterior draws of the trend."));
    }
    s.irregular = h5.exist("/posterior/cycle/coeffs");
    s.k = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("k"), 0);
    if (s.k < 1)
    {
        throw failure(QObject::tr("The file gives no series."));
    }
    s.series = readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables"));
    for (int i = static_cast<int>(s.series.size()); i < s.k; ++i)
    {
        s.series << QObject::tr("Series %1").arg(i + 1);
    }
    return s;
}

} // namespace

TrendCycleSupport trendCycleSupport(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        TrendCycleSupport s = readSupport(file);
        s.periods = static_cast<int>(panelByPeriod(file, s.k).n_cols);
        s.draws = static_cast<int>(readMatrix(file, QStringLiteral("/posterior/trend/coeffs")).cols);
        s.time = samplePeriodTimes(file, s.periods);
        s.ok = true;
        return s;
    }
    catch (const std::exception &e)
    {
        TrendCycleSupport out;
        out.reason = QString::fromUtf8(e.what());
        return out;
    }
}

std::vector<TrendCycleSeries> trendCycle(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const TrendCycleSupport s = readSupport(file);
    const arma::mat y = panelByPeriod(file, s.k);
    const int tt = static_cast<int>(y.n_cols);
    const arma::mat trend = readDraws(file, QStringLiteral("/posterior/trend/coeffs"));
    if (trend.n_rows != static_cast<arma::uword>(s.k * tt))
    {
        throw failure(QObject::tr("The trend's draws are not k x %1 periods long.").arg(tt));
    }
    // The cycle as the model has it: drawn with the trend beside an irregular
    // component, and otherwise all of the data the trend leaves.
    arma::mat cycle;
    if (s.irregular)
    {
        cycle = readDraws(file, QStringLiteral("/posterior/cycle/coeffs"));
    }
    else
    {
        cycle = -trend;
        const arma::vec stacked = arma::vectorise(y);   // period by period, as the draws are
        cycle.each_col() += stacked;
    }
    std::vector<TrendCycleSeries> out;
    for (int i = 0; i < s.k; ++i)
    {
        TrendCycleSeries series;
        series.data = arma::conv_to<std::vector<double>>::from(arma::rowvec(y.row(static_cast<arma::uword>(i))));
        series.trend = summarisePath(trend, s.k, i, tt);
        series.cycle = summarisePath(cycle, s.k, i, tt);
        out.push_back(std::move(series));
    }
    return out;
}

} // namespace studio
