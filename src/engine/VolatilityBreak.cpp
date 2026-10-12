#include "engine/VolatilityBreak.h"

#include "engine/ModelFileTree.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace studio
{

namespace
{

double scalar(const HighFive::File &h5, const std::string &path, double fallback)
{
    if (!h5.exist(path))
    {
        return fallback;
    }
    const HighFive::DataSet ds = h5.getDataSet(path);
    if (ds.getElementCount() < 1)
    {
        return fallback;
    }
    std::vector<double> v(ds.getElementCount());
    ds.read_raw(v.data());
    return v.front();
}

} // namespace

QString VolatilityBreakSummary::labelOf(int c) const
{
    if (!dated)
    {
        return QString::number(c);
    }
    // The period's index from the start of its year, rounded against the
    // floating point of tsp.
    const double t = timeOf(c);
    const long long whole = std::llround(t * frequency);
    const long long year = whole / static_cast<long long>(std::llround(frequency));
    const long long sub = whole % static_cast<long long>(std::llround(frequency)) + 1;
    if (std::llround(frequency) == 12)
    {
        return QStringLiteral("%1-%2").arg(year).arg(sub, 2, 10, QLatin1Char('0'));
    }
    if (std::llround(frequency) == 4)
    {
        return QStringLiteral("%1 Q%2").arg(year).arg(sub);
    }
    if (std::llround(frequency) == 1)
    {
        return QString::number(year);
    }
    return QStringLiteral("%1.%2").arg(year).arg(sub);
}

double breakScale(const arma::vec &draw, int scales, int j)
{
    if (j < 0)
    {
        return 1.0;
    }
    if (j < scales)
    {
        return draw(static_cast<arma::uword>(j));
    }
    const double last = draw(static_cast<arma::uword>(scales - 1));
    const double rho = draw(static_cast<arma::uword>(scales));
    return 1.0 + (last - 1.0) * std::pow(rho, j - scales + 1);
}

bool hasVolatilityBreak(const QString &file)
{
    if (file.isEmpty())
    {
        return false;
    }
    try
    {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        return h5.exist("/posterior/volatility/coeffs");
    }
    catch (const std::exception &)
    {
        return false;
    }
}

VolatilityBreakSummary volatilityBreakSummary(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    VolatilityBreakSummary out;
    out.breakColumn = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("volatility_break"), 0);
    out.scales = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("volatility_scales"), 3);
    out.horizon = std::max(0, readGroupInt(file, QStringLiteral("/model"), QStringLiteral("h"), 0));
    const int k = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("k"), 0);
    if (out.breakColumn < 1 || out.scales < 1)
    {
        throw std::runtime_error(QObject::tr("The model has no volatility break.").toStdString());
    }
    const Matrix draws = readMatrix(file, QStringLiteral("/posterior/volatility/coeffs"));
    if (draws.rows != static_cast<std::size_t>(out.scales + 1) || draws.cols == 0)
    {
        throw std::runtime_error(QObject::tr("The draws of the volatility break are not those of %1 scales and ρ.")
                                     .arg(out.scales)
                                     .toStdString());
    }
    const Matrix y = readMatrix(file, QStringLiteral("/data/train/y"));
    out.periods = k > 0 ? static_cast<int>(y.rows * y.cols) / k : static_cast<int>(y.cols);
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::DataSet ds = h5.getDataSet("/data/train/y");
        if (ds.hasAttribute("tsp"))
        {
            const auto tsp = ds.getAttribute("tsp").read<std::vector<double>>();
            if (tsp.size() == 3 && tsp[2] > 0)
            {
                out.start = tsp[0];
                out.frequency = tsp[2];
                out.dated = true;
            }
        }
        out.shape = scalar(h5, "/priors/volatility/shape", 0.0);
        out.rhoShape1 = scalar(h5, "/priors/volatility/rho_shape1", 0.0);
        out.rhoShape2 = scalar(h5, "/priors/volatility/rho_shape2", 0.0);
    }

    const auto m = static_cast<arma::uword>(out.scales);
    out.draws = static_cast<int>(draws.cols);
    // Row-major (quantity, draw) as HDF5 stores it into one column per draw.
    out.parameters = arma::mat(draws.cols, draws.rows);
    std::copy(draws.values.begin(), draws.values.end(), out.parameters.begin());
    out.parameters = arma::mat(out.parameters.t());
    for (int j = 0; j < out.scales; ++j)
    {
        out.labels << QObject::tr("s, %1").arg(out.labelOf(out.breakColumn + j));
    }
    out.labels << QStringLiteral("ρ");

    const arma::vec probs = {0.05, 0.16, 0.5, 0.84, 0.95};
    out.table.set_size(m + 1, 7);
    for (arma::uword r = 0; r <= m; ++r)
    {
        const arma::rowvec row = out.parameters.row(r);
        out.table(r, 0) = arma::mean(row);
        out.table(r, 1) = row.n_elem > 1 ? arma::stddev(row) : 0.0;
        const arma::vec q = arma::quantile(arma::vec(row.t()), probs);
        out.table.row(r).cols(2, 6) = q.t();
    }

    // The path: from a few periods before the break -- where the scale is one
    // -- to the end of the forecast horizon.
    const int before = std::min(out.breakColumn - 1, std::max(3, static_cast<int>(std::lround(out.frequency / 2))));
    out.firstColumn = out.breakColumn - before;
    const int last = out.periods + out.horizon;
    out.sampleEnd = out.timeOf(out.periods);
    const int n = std::max(0, last - out.firstColumn + 1);
    out.path.set_size(static_cast<arma::uword>(n), 5);
    arma::vec s(draws.cols);
    for (int i = 0; i < n; ++i)
    {
        const int c = out.firstColumn + i;
        out.time.push_back(out.timeOf(c));
        for (arma::uword d = 0; d < draws.cols; ++d)
        {
            s(d) = breakScale(out.parameters.col(d), out.scales, c - out.breakColumn);
        }
        out.path.row(static_cast<arma::uword>(i)) = arma::quantile(s, probs).t();
    }
    return out;
}

} // namespace studio
