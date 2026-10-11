#include "engine/Criteria.h"

#include "engine/ModelFileTree.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{

/// log mean_d exp(x_d), stable for large |x|.
double logMeanExp(const Matrix &m, std::size_t row)
{
    double hi = -INFINITY;
    for (std::size_t d = 0; d < m.cols; ++d)
    {
        hi = std::max(hi, m.at(row, d));
    }
    double sum = 0.0;
    for (std::size_t d = 0; d < m.cols; ++d)
    {
        sum += std::exp(m.at(row, d) - hi);
    }
    return hi + std::log(sum / static_cast<double>(m.cols));
}

bool exists(const QString &file, const char *path)
{
    HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    return h5.exist(path);
}

} // namespace

Criteria computeCriteria(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    Criteria c;
    try
    {
        if (exists(file, "/posterior/loglik"))
        {
            const Matrix ll = readMatrix(file, QStringLiteral("/posterior/loglik"));
            c.periods = static_cast<int>(ll.rows);
            c.draws = static_cast<int>(ll.cols);
            if (ll.cols > 1)
            {
                double lppd = 0.0;
                double p = 0.0;
                for (std::size_t t = 0; t < ll.rows; ++t)
                {
                    lppd += logMeanExp(ll, t);
                    double mean = 0.0;
                    for (std::size_t d = 0; d < ll.cols; ++d)
                    {
                        mean += ll.at(t, d);
                    }
                    mean /= static_cast<double>(ll.cols);
                    double ss = 0.0;
                    for (std::size_t d = 0; d < ll.cols; ++d)
                    {
                        ss += (ll.at(t, d) - mean) * (ll.at(t, d) - mean);
                    }
                    p += ss / static_cast<double>(ll.cols - 1);
                }
                c.lppd = lppd;
                c.pWaic = p;
                c.waic = -2.0 * (lppd - p);
            }
            else
            {
                // The discounted models write the exact predictive density per
                // period, one column: its sum is the marginal likelihood.
                double sum = 0.0;
                for (std::size_t t = 0; t < ll.rows; ++t)
                {
                    sum += ll.at(t, 0);
                }
                c.lppd = sum;
                c.note = QObject::tr("One column: the sum is the log marginal likelihood.");
            }
        }
        else
        {
            c.note = QObject::tr("No log likelihood yet.");
        }

        if (exists(file, "/posterior/forecast/loglik"))
        {
            const Matrix score = readMatrix(file, QStringLiteral("/posterior/forecast/loglik"));
            double total = 0.0;
            for (std::size_t r = 0; r < score.rows; ++r)
            {
                total += score.cols > 1 ? logMeanExp(score, r) : score.at(r, 0);
            }
            c.predictiveScore = total;
        }
    }
    catch (const std::exception &e)
    {
        c.note = QString::fromUtf8(e.what());
    }
    return c;
}

} // namespace studio
