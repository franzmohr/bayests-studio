// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/StateVariance.h"

#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"

#include <QFile>
#include <QObject>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>

#include "io/hdf5/hdf5_and_armadillo.h"

namespace studio
{

namespace
{

/// The tight shape that turns the centred prior into a set variance: against
/// it the T/2 the data add to the posterior shape are noise.
constexpr double fixedShape = 1e6;

std::vector<double> row(const QString &file, const QString &path)
{
    const Matrix m = readMatrix(file, path);
    return m.values;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double quantileOfSorted(const std::vector<double> &sorted, double q)
{
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (pos - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

void replaceRow(HighFive::File &h5, const ModelFile &mf, const std::string &path, const arma::vec &v)
{
    if (h5.exist(path))
    {
        h5.unlink(path);
    }
    write_armadillo_matrix_to_hdf5(mf, path, arma::mat(v), false);   // a single row on disk
}

void replaceIntAttribute(HighFive::File &h5, const std::string &group, const std::string &name, int value)
{
    HighFive::Group g = h5.getGroup(group);
    if (g.hasAttribute(name))
    {
        g.deleteAttribute(name);
    }
    g.createAttribute<int>(name, HighFive::DataSpace::From(value)).write(value);
}

} // namespace

StateVariancePrior readStateVariancePrior(const QString &file)
{
    StateVariancePrior out;
    const CheckReport report = ModelRunner::check(file, QString());
    if (!report.ok)
    {
        out.refusal = report.error;
        return out;
    }
    const bool tvp = report.algorithm.startsWith(QLatin1String("VarTvp")) ||
                     report.algorithm.startsWith(QLatin1String("VecTvp"));
    if (!tvp || !report.timeVarying || report.algorithm.endsWith(QLatin1String("Discount")))
    {
        out.refusal = report.algorithm.endsWith(QLatin1String("Discount"))
                          ? QObject::tr("A discounted model has no state variance: the discount factors play its part.")
                          : QObject::tr("The state variance explorer needs a VAR or VEC with time-varying coefficients.");
        return out;
    }
    out.periods = report.periods;
    out.iterations = report.iterations;
    out.burnin = report.burnin;
    try
    {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        out.nonCentred = h5.exist("/priors/a/omega_v");
        if (out.nonCentred)
        {
            out.level = row(file, QStringLiteral("/priors/a/omega_v"));
        }
        else if (h5.exist("/priors/a/shape") && h5.exist("/priors/a/rate"))
        {
            const std::vector<double> shape = row(file, QStringLiteral("/priors/a/shape"));
            out.level = row(file, QStringLiteral("/priors/a/rate"));
            if (shape.size() != out.level.size())
            {
                throw std::runtime_error("shape and rate differ in length");
            }
            for (std::size_t i = 0; i < shape.size(); ++i)
            {
                out.level[i] /= shape[i];
            }
        }
    }
    catch (const std::exception &e)
    {
        out.refusal = QObject::tr("The prior on the state variances cannot be read: %1").arg(QString::fromUtf8(e.what()));
        return out;
    }
    if (out.level.empty())
    {
        out.refusal = QObject::tr("The model has no prior on its coefficients' state variances under /priors/a.");
        return out;
    }
    out.coefficients = static_cast<int>(out.level.size());
    out.typical = median(out.level);
    if (!(out.typical > 0.0))
    {
        out.refusal = QObject::tr("The prior's typical state variance is not positive.");
        return out;
    }
    out.available = true;
    return out;
}

void writeStateVarianceVariant(const QString &file, const QString &output, const StateVarianceRun &run)
{
    const StateVariancePrior prior = readStateVariancePrior(file);
    if (!prior.available)
    {
        throw std::runtime_error(prior.refusal.toStdString());
    }
    if (!(run.variance > 0.0) || !std::isfinite(run.variance))
    {
        throw std::runtime_error(QObject::tr("The state variance must be positive.").toStdString());
    }
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    if (QFile::exists(output) && !QFile::remove(output))
    {
        throw std::runtime_error(QObject::tr("Cannot replace %1.").arg(output).toStdString());
    }
    if (!QFile::copy(file, output))
    {
        throw std::runtime_error(QObject::tr("Cannot write %1.").arg(output).toStdString());
    }
    QFile::setPermissions(output, QFile::permissions(output) | QFileDevice::WriteOwner);

    HighFive::File h5(output.toStdString(), HighFive::File::ReadWrite);
    const ModelFile mf(h5);
    if (h5.exist("/posterior"))
    {
        h5.unlink("/posterior");
    }
    const double factor = run.variance / prior.typical;
    arma::vec level(prior.level);
    level *= factor;
    if (prior.nonCentred)
    {
        replaceRow(h5, mf, "/priors/a/omega_v", level);
    }
    else
    {
        arma::vec shape = run.fixed ? arma::vec(level.n_elem, arma::fill::value(fixedShape))
                                    : arma::vec(readMatrix(file, QStringLiteral("/priors/a/shape")).values);
        replaceRow(h5, mf, "/priors/a/shape", shape);
        replaceRow(h5, mf, "/priors/a/rate", level % shape);
    }
    if (h5.exist("/initial/a_sigma_inv"))
    {
        h5.unlink("/initial/a_sigma_inv");
        write_armadillo_matrix_to_hdf5(mf, "/initial/a_sigma_inv", arma::mat(arma::diagmat(1.0 / level)), false);
    }
    if (run.iterations > 0)
    {
        replaceIntAttribute(h5, "/model", "iterations", run.iterations);
    }
    if (run.burnin >= 0)
    {
        replaceIntAttribute(h5, "/model", "burnin", run.burnin);
    }
}

StatePathSummary summariseStatePath(const QString &file, int coefficient, int coefficients)
{
    StatePathSummary out;
    const Matrix path = readMatrix(file, QStringLiteral("/posterior/a/coeffs"));
    const auto k = static_cast<std::size_t>(coefficients);
    const auto c = static_cast<std::size_t>(coefficient);
    if (k == 0 || c >= k || path.rows % k != 0 || path.cols == 0)
    {
        throw std::runtime_error(QObject::tr("The coefficient path does not match the prior.").toStdString());
    }
    const std::size_t periods = path.rows / k;
    out.draws = static_cast<int>(path.cols);
    std::vector<double> column(path.cols);
    for (std::size_t t = 0; t < periods; ++t)
    {
        for (std::size_t d = 0; d < path.cols; ++d)
        {
            column[d] = path.at(t * k + c, d);
        }
        std::sort(column.begin(), column.end());
        out.lo90.push_back(quantileOfSorted(column, 0.05));
        out.lo68.push_back(quantileOfSorted(column, 0.16));
        out.median.push_back(quantileOfSorted(column, 0.5));
        out.hi68.push_back(quantileOfSorted(column, 0.84));
        out.hi90.push_back(quantileOfSorted(column, 0.95));
    }
    try
    {
        const Matrix sigma = readMatrix(file, QStringLiteral("/posterior/a/sigma"));
        if (c < sigma.rows)
        {
            for (std::size_t d = 0; d < sigma.cols; ++d)
            {
                out.stateSd.push_back(std::sqrt(std::max(0.0, sigma.at(c, d))));
            }
            std::sort(out.stateSd.begin(), out.stateSd.end());
        }
    }
    catch (const std::exception &)
    {
        out.stateSd.clear();   // a model that does not write it: the path still shows
    }
    return out;
}

} // namespace studio
