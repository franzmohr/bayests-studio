#include "engine/HorseRace.h"

#include "engine/ModelFileTree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace studio
{

namespace
{

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

QString slug(const QString &text)
{
    QString s;
    for (const QChar c : text.toLower())
    {
        if (c.isLetterOrNumber() && c.unicode() < 128)
        {
            s += c;
        }
        else if (!s.isEmpty() && !s.endsWith(QLatin1Char('-')))
        {
            s += QLatin1Char('-');
        }
    }
    while (s.endsWith(QLatin1Char('-')))
    {
        s.chop(1);
    }
    return s.isEmpty() ? QStringLiteral("model") : s.left(40);
}

double logMeanExp(const Matrix &m, std::size_t row)
{
    double hi = -std::numeric_limits<double>::infinity();
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
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        return h5.exist(path);
    }
    catch (const std::exception &)
    {
        return false;
    }
}

} // namespace

std::vector<int> HorseRaceDesign::origins() const
{
    std::vector<int> out;
    for (int o = firstOrigin; o <= lastOrigin && step > 0; o += step)
    {
        out.push_back(o);
    }
    return out;
}

int HorseRaceManifest::models() const
{
    int n = 0;
    for (const auto &c : files)
    {
        n += static_cast<int>(c.size());
    }
    return n;
}

int suggestedLags(const DataSet &data)
{
    const TimeIndex index = timeIndex(data, 0, std::max(0, data.length() - 1));
    if (!index.known)
    {
        return 4;
    }
    if (index.frequency >= 12)
    {
        return 12;
    }
    return index.frequency <= 1 ? 2 : 4;
}

std::vector<HorseRaceCandidate> standardCandidates(int lags, bool levels, int volatilityBreak,
                                                   const QString &breakLabel)
{
    using Errors = VarBuilderSpec::Errors;
    std::vector<HorseRaceCandidate> out;
    const auto base = [lags, levels]() {
        VarBuilderSpec s;
        s.p = lags;
        s.constant = true;
        s.minnesota = true;
        // For series in levels the Minnesota prior centres on random walks,
        // as Litterman's does; for growth rates on white noise.
        s.randomWalkMean = levels;
        s.horizon = 0;
        s.holdout = 0;
        return s;
    };
    {
        HorseRaceCandidate c{QObject::tr("BVAR, Minnesota prior"),
                             QObject::tr("The workhorse: constant coefficients, a Wishart error precision and "
                                         "the Minnesota prior of Litterman (1986). The usual benchmark."),
                             base()};
        out.push_back(c);
    }
    {
        HorseRaceCandidate c{QObject::tr("BVAR, stochastic volatility"),
                             QObject::tr("Constant coefficients, the error variances drifting as random walks "
                                         "(Clark 2011): wider bands after volatile periods."),
                             base()};
        c.spec.errors = Errors::Stochvol;
        c.spec.covar = true;
        out.push_back(c);
    }
    {
        HorseRaceCandidate c{QObject::tr("TVP-VAR, stochastic volatility"),
                             QObject::tr("Coefficients and volatilities both drift (Primiceri 2005); two lags, as "
                                         "the parameters multiply with every one."),
                             base()};
        c.spec.p = std::min(lags, 2);
        c.spec.minnesota = false;
        c.spec.timeVarying = true;
        c.spec.errors = Errors::Stochvol;
        c.spec.covar = true;
        out.push_back(c);
    }
    {
        HorseRaceCandidate c{QObject::tr("BVAR, variable selection"),
                             QObject::tr("Bayesian variable selection (Korobilis 2013) over the lags: the data "
                                         "decide which coefficients the forecast uses."),
                             base()};
        c.spec.minnesota = false;
        c.spec.coefPrecision = 1.0 / 9.0;
        c.spec.selection = VarBuilderSpec::Selection::Bvs;
        out.push_back(c);
    }
    {
        HorseRaceCandidate c{QObject::tr("BVAR, one lag"),
                             QObject::tr("A parsimonious VAR(1) under the Minnesota prior: how much do the "
                                         "further lags add?"),
                             base()};
        c.spec.p = 1;
        out.push_back(c);
    }
    if (levels)
    {
        HorseRaceCandidate c{QObject::tr("VEC, rank one"),
                             QObject::tr("The series in levels with one cointegration relation (Koop, León-"
                                         "González and Strachan 2010): forecasts that keep the long run."),
                             base()};
        c.spec.family = VarBuilderSpec::Family::Vec;
        c.spec.rank = 1;
        c.spec.minnesota = false;
        out.push_back(c);
    }
    if (volatilityBreak >= 0)
    {
        HorseRaceCandidate c{QObject::tr("BVAR, volatility break"),
                             QObject::tr("The benchmark with the volatility break of Lenza and Primiceri (2022): "
                                         "from %1 every shock's variance is scaled, three months freely and then "
                                         "decaying, so the outliers neither move the coefficients nor widen the "
                                         "bands for good. Before the break it is the benchmark.")
                                 .arg(breakLabel),
                             base()};
        c.spec.volatilityBreak = volatilityBreak;
        out.push_back(c);
    }
    return out;
}

QString horseRaceRefusal(const HorseRaceDesign &d)
{
    if (d.data.empty())
    {
        return QObject::tr("Import a data set first.");
    }
    if (d.columns.empty())
    {
        return QObject::tr("Choose the variables to forecast.");
    }
    if (d.candidates.size() < 2)
    {
        return QObject::tr("A horse race needs two models or more.");
    }
    if (d.benchmark < 0 || d.benchmark >= static_cast<int>(d.candidates.size()))
    {
        return QObject::tr("Choose a benchmark among the models.");
    }
    if (d.horizon < 1)
    {
        return QObject::tr("The horizon must be at least one period.");
    }
    if (d.firstOrigin > d.lastOrigin || d.step < 1)
    {
        return QObject::tr("The first forecast origin must come before the last.");
    }
    if (d.lastOrigin >= d.data.length() - 1)
    {
        return QObject::tr("The last origin must leave at least one period to score.");
    }
    // Every candidate must be a model the builder accepts at the first origin,
    // the shortest window.
    for (const HorseRaceCandidate &c : d.candidates)
    {
        VarBuilderSpec s = c.spec;
        s.data = d.data;
        s.columns = d.columns;
        s.firstRow = d.firstRow;
        s.lastRow = std::min(d.firstOrigin + d.horizon, d.data.length() - 1);
        s.horizon = d.horizon;
        s.holdout = s.lastRow - d.firstOrigin;
        s.iterations = d.iterations;
        s.burnin = d.burnin;
        const VarBuilderSummary summary = summarise(s);
        if (!summary.refusal.isEmpty())
        {
            return QObject::tr("%1: %2").arg(c.name, summary.refusal);
        }
    }
    return {};
}

HorseRaceManifest writeHorseRace(const HorseRaceDesign &d, const QString &folder,
                                 const std::function<bool(int, int)> &progress)
{
    if (const QString why = horseRaceRefusal(d); !why.isEmpty())
    {
        throw std::runtime_error(why.toStdString());
    }
    const std::vector<int> origins = d.origins();
    HorseRaceManifest m;
    m.folder = QDir(folder).absolutePath();
    m.benchmark = d.benchmark;
    m.horizon = d.horizon;
    for (const int c : d.columns)
    {
        m.variables << d.data.names[c];
    }
    for (const int o : origins)
    {
        m.origins << d.data.periodLabel(o);
        const TimeIndex t = timeIndex(d.data, o, o);
        m.originTimes.push_back(t.known ? t.start : static_cast<double>(o + 1));
    }
    QDir root(m.folder);
    root.mkpath(QStringLiteral("."));
    const int total = static_cast<int>(d.candidates.size() * origins.size());
    int done = 0;
    QStringList used;
    for (std::size_t ci = 0; ci < d.candidates.size(); ++ci)
    {
        const HorseRaceCandidate &c = d.candidates[ci];
        m.candidates << c.name;
        QString dir = slug(c.name);
        while (used.contains(dir))
        {
            dir += QLatin1Char('x');
        }
        used << dir;
        root.mkpath(dir);
        std::vector<QString> files;
        for (std::size_t oi = 0; oi < origins.size(); ++oi)
        {
            const int o = origins[oi];
            VarBuilderSpec s = c.spec;
            s.data = d.data;
            s.columns = d.columns;
            s.firstRow = d.firstRow;
            s.lastRow = std::min(o + d.horizon, d.data.length() - 1);
            s.horizon = d.horizon;
            s.holdout = s.lastRow - o;   // the estimation sample ends at the origin
            s.iterations = d.iterations;
            s.burnin = d.burnin;
            // Common random numbers: every candidate at an origin draws from
            // the same seed, so two models that coincide there -- the break
            // before its period and the benchmark -- draw identically, and a
            // difference between candidates is not simulation noise.
            s.seed = (d.seed + oi) % 2147483647ULL;
            const QString file = root.filePath(QStringLiteral("%1/%2.h5").arg(dir, slug(d.data.periodLabel(o))));
            writeVarModel(s, file);
            files.push_back(file);
            if (progress && !progress(++done, total))
            {
                throw std::runtime_error(QObject::tr("Cancelled.").toStdString());
            }
        }
        m.files.push_back(files);
    }

    QJsonObject json;
    json[QStringLiteral("benchmark")] = m.benchmark;
    json[QStringLiteral("horizon")] = m.horizon;
    json[QStringLiteral("variables")] = QJsonArray::fromStringList(m.variables);
    json[QStringLiteral("origins")] = QJsonArray::fromStringList(m.origins);
    QJsonArray times;
    for (const double t : m.originTimes)
    {
        times.append(t);
    }
    json[QStringLiteral("originTimes")] = times;
    QJsonArray candidates;
    for (std::size_t ci = 0; ci < d.candidates.size(); ++ci)
    {
        QJsonObject c;
        c[QStringLiteral("name")] = d.candidates[ci].name;
        c[QStringLiteral("description")] = d.candidates[ci].description;
        c[QStringLiteral("algorithm")] = d.candidates[ci].spec.algorithm();
        QJsonArray files;
        for (const QString &f : m.files[ci])
        {
            files.append(root.relativeFilePath(f));
        }
        c[QStringLiteral("files")] = files;
        candidates.append(c);
    }
    json[QStringLiteral("candidates")] = candidates;
    QFile out(root.filePath(QStringLiteral("horse-race.json")));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        throw std::runtime_error(QObject::tr("Cannot write the manifest in %1.").arg(m.folder).toStdString());
    }
    out.write(QJsonDocument(json).toJson());
    return m;
}

HorseRaceManifest readHorseRace(const QString &folder)
{
    QFile in(QDir(folder).filePath(QStringLiteral("horse-race.json")));
    if (!in.open(QIODevice::ReadOnly))
    {
        throw std::runtime_error(QObject::tr("%1 holds no horse race.").arg(folder).toStdString());
    }
    const QJsonObject json = QJsonDocument::fromJson(in.readAll()).object();
    HorseRaceManifest m;
    m.folder = QDir(folder).absolutePath();
    m.benchmark = json[QStringLiteral("benchmark")].toInt();
    m.horizon = json[QStringLiteral("horizon")].toInt();
    for (const auto v : json[QStringLiteral("variables")].toArray())
    {
        m.variables << v.toString();
    }
    for (const auto v : json[QStringLiteral("origins")].toArray())
    {
        m.origins << v.toString();
    }
    for (const auto v : json[QStringLiteral("originTimes")].toArray())
    {
        m.originTimes.push_back(v.toDouble());
    }
    const QDir root(m.folder);
    for (const auto c : json[QStringLiteral("candidates")].toArray())
    {
        const QJsonObject o = c.toObject();
        m.candidates << o[QStringLiteral("name")].toString();
        std::vector<QString> files;
        for (const auto f : o[QStringLiteral("files")].toArray())
        {
            files.push_back(root.filePath(f.toString()));
        }
        m.files.push_back(files);
    }
    return m;
}

HorseRaceScores scoreHorseRace(const HorseRaceManifest &m)
{
    HorseRaceScores out;
    out.manifest = m;
    const auto k = static_cast<arma::uword>(m.variables.size());
    const auto h = static_cast<arma::uword>(m.horizon);
    for (const auto &files : m.files)
    {
        arma::mat squared(k, h, arma::fill::zeros);
        arma::mat count(k, h, arma::fill::zeros);
        arma::vec score(files.size(), arma::fill::value(NaN));
        int complete = 0;
        for (std::size_t oi = 0; oi < files.size(); ++oi)
        {
            const QString &file = files[oi];
            if (!exists(file, "/posterior/forecast/forecasts"))
            {
                continue;
            }
            ++complete;
            // (k h, draws): variable i at horizon j in row j k + i.
            const Matrix forecasts = readMatrix(file, QStringLiteral("/posterior/forecast/forecasts"));
            if (exists(file, "/data/test/y"))
            {
                // (k, observed horizons), levels for a VEC as its forecast is.
                const Matrix realised = readMatrix(file, QStringLiteral("/data/test/y"));
                const std::size_t observed = std::min<std::size_t>(realised.cols, h);
                for (std::size_t j = 0; j < observed; ++j)
                {
                    for (std::size_t i = 0; i < k; ++i)
                    {
                        double mean = 0.0;
                        for (std::size_t d = 0; d < forecasts.cols; ++d)
                        {
                            mean += forecasts.at(j * k + i, d);
                        }
                        mean /= static_cast<double>(forecasts.cols);
                        const double e = realised.at(i, j) - mean;
                        squared(i, j) += e * e;
                        count(i, j) += 1.0;
                    }
                }
            }
            if (exists(file, "/posterior/forecast/loglik"))
            {
                const Matrix ll = readMatrix(file, QStringLiteral("/posterior/forecast/loglik"));
                double total = 0.0;
                for (std::size_t r = 0; r < ll.rows; ++r)
                {
                    total += ll.cols > 1 ? logMeanExp(ll, r) : ll.at(r, 0);
                }
                score(oi) = total;
            }
        }
        arma::mat rmse = arma::sqrt(squared / count);
        rmse.elem(arma::find(count == 0)).fill(NaN);
        out.rmse.push_back(rmse);
        out.observed.push_back(count);
        out.logScore.push_back(score);
        out.complete.push_back(complete);
    }
    return out;
}

} // namespace studio
