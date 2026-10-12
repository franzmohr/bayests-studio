#include "engine/Examples.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTextStream>

#include <cmath>
#include <stdexcept>

namespace studio
{

namespace
{

std::vector<WorkflowExample> make()
{
    std::vector<WorkflowExample> out;

    // -- Arias, Rubio-Ramírez and Waggoner (2018): the optimism shock.
    {
        WorkflowExample e;
        e.id = QStringLiteral("arw2018");
        e.workflow = QStringLiteral("thesis");
        e.title = QObject::tr("Optimism shocks: Arias, Rubio-Ramírez and Waggoner (2018)");
        e.reference = QStringLiteral("Arias, J. E., Rubio-Ramírez, J. F. and Waggoner, D. F. (2018). Inference "
                                     "Based on Structural Vector Autoregressions Identified with Sign and Zero "
                                     "Restrictions: Theory and Applications. Econometrica 86(2), 685-720.");
        e.question = QObject::tr("Do optimism shocks -- news that raises stock prices without moving TFP on "
                                 "impact -- drive consumption and hours, as Beaudry, Nam and Wang (2011) found "
                                 "with a penalty function?");
        e.folder = QStringLiteral("2018 Arias et al Inference Based on structural VARs");
        e.recipe.file = e.folder + QStringLiteral("/replication_files_v5.0/data/data.csv");
        e.recipe.dateColumn = QStringLiteral("date");
        e.recipe.dates = DataRecipe::Dates::YearDotQuarter;
        e.recipe.columns = {{QStringLiteral("bnw_bfk_tfp_util"), QStringLiteral("tfp")},
                            {QStringLiteral("bnw_percap_rsp_end"), QStringLiteral("stocks")},
                            {QStringLiteral("bnw_percap_c"), QStringLiteral("cons")},
                            {QStringLiteral("bnw_expost_ffr_mon"), QStringLiteral("rate")},
                            {QStringLiteral("bnw_percap_h"), QStringLiteral("hours")}};
        e.recipe.scale = 100.0;
        e.p = 4;
        e.coefPrecision = 1e-10;   // the paper's flat prior
        e.detPrecision = 1e-10;
        e.wishartScale = 1e-6;
        e.iterations = 5000;
        e.burnin = 1000;
        e.seed = 20180212;
        e.restrictions = {{QStringLiteral("tfp"), QStringLiteral("tfp"), 0, 0},
                          {QStringLiteral("tfp"), QStringLiteral("stocks"), 1, 0}};
        e.notes = {
            {QStringLiteral("question"),
             QObject::tr("The data are Beaudry, Nam and Wang's: utilisation-adjusted TFP, real stock prices, "
                         "consumption, the real interest rate and hours, quarterly 1955Q1 to 2010Q4, per capita and "
                         "in logs. The assistant reads them from the paper's replication files and multiplies "
                         "them by 100, as the reproduction does.")},
            {QStringLiteral("inspect"),
             QObject::tr("TFP, stock prices, consumption and hours trend; the paper keeps them in log levels, as a "
                         "Bayesian VAR in levels may (Sims, Stock and Watson 1990). The real rate does not.")},
            {QStringLiteral("baseline"),
             QObject::tr("Four lags and a constant, in the order tfp, stocks, cons, rate, hours, with the paper's "
                         "flat prior: precisions of 1e-10 on the coefficients and a Wishart scale of 1e-6 -- the "
                         "data alone speak. 5,000 draws after 1,000, seed 20180212.")},
            {QStringLiteral("lags"), QObject::tr("The paper fixes four lags; the comparison is for practice.")},
            {QStringLiteral("identify"),
             QObject::tr("The optimism shock is the shock that leaves TFP unchanged on impact (a zero) and raises "
                         "stock prices (a sign). A zero restriction needs the importance sampler of the paper "
                         "itself: the assistant opens Sign Restrictions with the grid filled in, and the "
                         "resampled model it writes joins the project.")},
            {QStringLiteral("responses"),
             QObject::tr("Read the responses to the stocks-named column, 40 quarters, 68% bands: Figure 1 (b) of "
                         "the paper.")},
        };
        e.findings = QObject::tr("TFP's impact response is exactly zero in every draw. The bands of the consumption "
                                 "and hours responses include zero: identified by the restrictions alone, without the "
                                 "penalty function, optimism shocks do not drive business cycles -- the paper's "
                                 "point. The effective sample size is about four fifths of the draws.");
        out.push_back(e);
    }

    // -- Nam and Wang (2018): the same shock with more signs.
    {
        WorkflowExample e = out.back();
        e.id = QStringLiteral("nam2018");
        e.title = QObject::tr("Mood swings: Nam and Wang (2018)");
        e.reference = QStringLiteral("Nam, D. and Wang, J. (2018). Mood Swings and Business Cycles: Evidence from "
                                     "Sign Restrictions. Journal of Money, Credit and Banking 51(6), 1623-1649.");
        e.question = QObject::tr("With signs on consumption and the interest rate as well, how much of the "
                                 "fluctuations in hours do optimism shocks explain?");
        e.folder = QStringLiteral("2018-NAM");
        e.iterations = 10000;
        e.burnin = 2000;
        e.seed = 20181214;
        e.restrictions = {{QStringLiteral("tfp"), QStringLiteral("tfp"), 0, 0},
                          {QStringLiteral("tfp"), QStringLiteral("stocks"), 1, 0},
                          {QStringLiteral("tfp"), QStringLiteral("cons"), 1, 0},
                          {QStringLiteral("tfp"), QStringLiteral("rate"), 1, 0}};
        e.notes[QStringLiteral("question")] =
            QObject::tr("The data are those of Arias, Rubio-Ramírez and Waggoner's replication, 1955Q1 to 2010Q4; "
                        "the paper's own sample runs to 2016Q4, six years longer.");
        e.notes[QStringLiteral("baseline")] =
            QObject::tr("As Arias et al.: four lags, a constant, the flat prior; 10,000 draws after 2,000, seed "
                        "20181214.");
        e.notes[QStringLiteral("identify")] =
            QObject::tr("The optimism shock leaves TFP unchanged on impact and raises stock prices, consumption "
                        "and the interest rate: one zero and three signs, filled in for you.");
        e.notes[QStringLiteral("responses")] =
            QObject::tr("Table 1 of the paper is the variance decomposition of hours: open the variance "
                        "decomposition, choose hours, and read the share of the restricted shock at horizons 8 "
                        "and 16. Figure 2 is the responses over 40 quarters.");
        e.findings = QObject::tr("Optimism shocks explain about a third of the forecast error variance of hours at "
                                 "two to four years -- every published share of Table 1 lies within the "
                                 "reproduction's bands -- and TFP falls below zero for about eight quarters.");
        out.push_back(e);
    }

    // -- Leduc and Liu (2016): uncertainty shocks, recursively identified.
    {
        WorkflowExample e;
        e.id = QStringLiteral("leducliu2016");
        e.workflow = QStringLiteral("thesis");
        e.title = QObject::tr("Uncertainty shocks: Leduc and Liu (2016)");
        e.reference = QStringLiteral("Leduc, S. and Liu, Z. (2016). Uncertainty Shocks Are Aggregate Demand Shocks. "
                                     "Journal of Monetary Economics 82, 20-35.");
        e.question = QObject::tr("Does a rise in uncertainty act like a negative aggregate demand shock -- raising "
                                 "unemployment and lowering inflation and the interest rate?");
        e.folder = QStringLiteral("2016-leduc et al-uncertainty");
        e.series = {QStringLiteral("uncertainty"), QStringLiteral("unemployment"), QStringLiteral("inflation"),
                    QStringLiteral("tbill")};
        e.p = 12;
        e.coefPrecision = 1e-10;   // the paper's flat prior
        e.detPrecision = 1e-10;
        e.wishartScale = 1e-6;
        e.iterations = 10000;
        e.burnin = 2000;
        e.seed = 20160715;
        e.notes = {
            {QStringLiteral("question"),
             QObject::tr("The paper comes without data, so you assemble them -- part of the exercise. Four monthly "
                         "series, January 1978 to October 2013, in a CSV with a first column of dates (1978-01) and "
                         "these column names, in this order: uncertainty -- the share of respondents of the "
                         "University of Michigan Survey of Consumers who name an 'uncertain future' as a reason it "
                         "is a bad time to buy a vehicle (sca.isr.umich.edu, the time-series tables on buying "
                         "conditions); unemployment -- the civilian unemployment rate (FRED UNRATE); inflation -- "
                         "the year-on-year change of the CPI in percent, 100 × (log CPI(t) − log CPI(t−12)) from "
                         "FRED CPIAUCSL; tbill -- the three-month Treasury bill rate (FRED TB3MS). The paper's "
                         "robustness check replaces uncertainty by the VIX (FRED VIXCLS, VXOCLS before 1990) from "
                         "1986.")},
            {QStringLiteral("inspect"),
             QObject::tr("Uncertainty is countercyclical and jumps in recessions (Figure 1 of the paper plots its "
                         "three-month average against the VIX); the T-bill rate sits at the zero lower bound from "
                         "2009, which the paper's appendix addresses with the two-year yield.")},
            {QStringLiteral("baseline"),
             QObject::tr("Uncertainty first, then unemployment, inflation and the T-bill rate; a constant and the "
                         "paper's flat prior. The paper does not state its lag order: twelve, a year of monthly "
                         "lags, is the convention for such VARs, and the lag comparison step lets you check it.")},
            {QStringLiteral("identify"),
             QObject::tr("Recursive, with uncertainty ordered first: the survey's interviews do not know the current "
                         "month's macroeconomic releases, so uncertainty does not respond to them within the month, "
                         "while unemployment, inflation and the rate may respond to an uncertainty shock at once. The "
                         "baseline's variable order is the identification: no sign restrictions.")},
            {QStringLiteral("responses"),
             QObject::tr("Orthogonalised responses to the uncertainty shock, 48 months, with 90% bands: Figure 2 of "
                         "the paper.")},
        };
        e.findings = QObject::tr("In the paper, unemployment rises by about 0.16 percentage points at its peak, around "
                                 "18 months after the shock, significantly for about three years; inflation falls by "
                                 "about 0.18 points at around 20 months, significantly from about the eighth month "
                                 "for two years; and the T-bill rate falls -- the pattern of an aggregate demand "
                                 "shock. The responses on impact are small, as the ordering implies.");
        out.push_back(e);
    }

    // -- A horse race on the data of Lenza and Primiceri (2022).
    {
        WorkflowExample e;
        e.id = QStringLiteral("lp2022race");
        e.workflow = QStringLiteral("horserace");
        e.title = QObject::tr("Forecasting through the pandemic: the data of Lenza and Primiceri (2022)");
        e.reference = QStringLiteral("Lenza, M. and Primiceri, G. E. (2022). How to Estimate a Vector "
                                     "Autoregression after March 2020. Journal of Applied Econometrics 37(4), "
                                     "688-699.");
        e.question = QObject::tr("How do standard BVARs forecast US unemployment, employment and PCE inflation "
                                 "from 2015 through the pandemic, and do stochastic volatility or the paper's "
                                 "volatility break rescue the forecasts once the March 2020 outliers arrive?");
        e.folder = QStringLiteral("2022-Lenza Primiceri-How to estimate vector autoregression after March 2020");
        e.recipe.file = e.folder + QStringLiteral("/data/lp_data.csv");
        e.recipe.dateColumn = QStringLiteral("DATE");
        e.recipe.dates = DataRecipe::Dates::IsoDate;
        e.recipe.columns = {{QStringLiteral("unemployment"), QStringLiteral("unemployment")},
                            {QStringLiteral("employment"), QStringLiteral("employment")},
                            {QStringLiteral("pce_price"), QStringLiteral("pce_price")},
                            {QStringLiteral("core_pce_price"), QStringLiteral("core_pce_price")}};
        e.recipe.first = QStringLiteral("1988-12");
        e.variables = {QStringLiteral("unemployment"), QStringLiteral("employment"), QStringLiteral("pce_price"),
                       QStringLiteral("core_pce_price")};
        e.firstOrigin = QStringLiteral("2015-01");
        e.lastOrigin = QStringLiteral("2021-03");
        e.step = 3;
        e.horizon = 12;
        e.p = 12;
        e.iterations = 2000;
        e.burnin = 500;
        // The series are in levels: random walk priors, and a VEC. Minnesota,
        // SV, one lag, the VEC and the volatility break from March 2020 -- the
        // TVP-VAR is slow at a monthly frequency, and selection on levels
        // would shrink towards zero.
        e.levels = true;
        e.volatilityBreak = QStringLiteral("2020-03");
        e.candidates = {0, 1, 4, 5, 6};
        e.benchmark = 0;
        e.notes = {
            {QStringLiteral("data"),
             QObject::tr("The paper's data, prepared by its reproduction (data/lp_data.csv): the unemployment rate, "
                         "and 100 × log of payroll employment, the PCE price index and core PCE prices, monthly. "
                         "The windows start in 1988-12, as the paper's estimation does.")},
            {QStringLiteral("design"),
             QObject::tr("Origins every quarter from 2015-01 to 2021-03, twelve months ahead, five models -- the "
                         "Minnesota BVAR as benchmark, centred on random walks as the series are levels, the same "
                         "with stochastic volatility, one lag, a VEC of rank one, and the benchmark with the "
                         "paper's volatility break from 2020-03 -- with twelve lags, as the paper. 2,000 draws "
                         "after 500 keep the 125 runs to minutes.")},
            {QStringLiteral("results"),
             QObject::tr("Read the totals with the origins in mind. The four origins from 2019-04 to 2020-01 have "
                         "April 2020 in their test window, and every model misses it by about eleven points of "
                         "unemployment; those origins alone decide the ranking on both measures. Up to 2020-01 "
                         "the break model is the benchmark, draw for draw, so every difference between the two "
                         "comes from the origins after the break.")},
            {QStringLiteral("stability"),
             QObject::tr("The cumulative log scores run together until 2019 and then fall off a cliff at once; the "
                         "one-lag model, whose predictive bands are widest, falls least. The break model's line "
                         "lies on the benchmark's until 2020-04 and climbs above it from there.")},
        };
        e.findings = QObject::tr(
            "Up to the 2019-01 origin the models are close: the stochastic volatility BVAR leads on density (a total "
            "log score of about 679 against the benchmark's 635) and nobody beats the benchmark's unemployment RMSE "
            "by much. The origins whose test window contains April 2020 then cost every model tens of thousands of "
            "log points -- no model forecasts a pandemic -- and decide the totals, the one-lag model losing least. "
            "Once the April observation is in the sample the paper's point shows: the benchmark extrapolates it, "
            "its twelve-month unemployment forecast from 2020-04 off by hundreds of points, and the VEC explodes, "
            "while the volatility break keeps that miss to about 27 points, stochastic volatility to about 29 and "
            "one lag to about 9. Over the four origins from 2020-04 the break model has the best log score of the "
            "field (about -104, against -142 for one lag, -159 for stochastic volatility and -200 for the "
            "benchmark), and from the 2020-10 origin its unemployment forecasts miss by at most about two thirds of "
            "a point, where the benchmark's miss by up to 2.4.");
        out.push_back(e);
    }

    // -- The papers the studio cannot yet implement step by step.
    const auto later = [&](const QString &id, const QString &workflow, const QString &title, const QString &folder,
                           const QString &missing) {
        WorkflowExample e;
        e.id = id;
        e.workflow = workflow;
        e.title = title;
        e.folder = folder;
        e.supported = false;
        e.missing = missing;
        out.push_back(e);
    };
    later(QStringLiteral("jk2020"), QStringLiteral("thesis"),
          QObject::tr("Monetary policy and information shocks: Jarociński and Karadi (2020)"),
          QStringLiteral("2020-jarocinski karadi"),
          QObject::tr("High-frequency surprises as equations without coefficients (n_iid), their missing months "
                      "estimated, and a rotation of two shocks only, the others recursive."));
    later(QStringLiteral("pinchetti2026"), QStringLiteral("thesis"),
          QObject::tr("Geopolitical risk and inflation: Pinchetti (2026)"), QStringLiteral("2026-pinchetti"),
          QObject::tr("As Jarociński and Karadi, and sign restrictions with thresholds over three horizons and a "
                      "Minnesota prior with quadratic lag decay."));
    later(QStringLiteral("esrb2026"), QStringLiteral("thesis"),
          QObject::tr("Political risk and euro area markets: ESRB (2026)"),
          QStringLiteral("2026-esrb-geoeconomic fragmentation"),
          QObject::tr("The daily data are built by the report's scripts (Polymarket index, GARCH volatilities); "
                      "once they are written as a CSV, the political risk BVAR and its sign and zero "
                      "restrictions are the thesis workflow's."));
    later(QStringLiteral("lp2022"), QStringLiteral("horserace"),
          QObject::tr("The paper's own model: Lenza and Primiceri (2022)"),
          QStringLiteral("2022-Lenza Primiceri-How to estimate vector autoregression after March 2020"),
          QObject::tr("The volatility break races in the horse race example; what is missing is the paper's "
                      "hierarchical prior, whose tightness is estimated with the scales, and its scenario "
                      "forecasts."));
    return out;
}

/// "1955.1" -> "1955 Q1"; "1947-01-01" -> "1947-01".
QString convertDate(const QString &raw, DataRecipe::Dates style)
{
    const QString t = raw.trimmed();
    if (style == DataRecipe::Dates::YearDotQuarter)
    {
        const qsizetype dot = t.indexOf(QLatin1Char('.'));
        if (dot < 0)
        {
            return t;
        }
        return QStringLiteral("%1 Q%2").arg(t.left(dot), t.mid(dot + 1, 1));
    }
    return t.left(7);
}

} // namespace

const std::vector<WorkflowExample> &builtinExamples()
{
    static const std::vector<WorkflowExample> all = make();
    return all;
}

namespace
{

std::vector<WorkflowExample> &registry()
{
    static std::vector<WorkflowExample> all = builtinExamples();
    return all;
}

} // namespace

const std::vector<WorkflowExample> &examples()
{
    return registry();
}

void setExtensionExamples(std::vector<WorkflowExample> extensions)
{
    std::vector<WorkflowExample> all = builtinExamples();
    for (WorkflowExample &e : extensions)
    {
        const auto same = std::find_if(all.begin(), all.end(), [&](const WorkflowExample &b) { return b.id == e.id; });
        if (same != all.end())
        {
            *same = std::move(e);
        }
        else
        {
            all.push_back(std::move(e));
        }
    }
    registry() = std::move(all);
}

std::vector<const WorkflowExample *> examplesFor(const QString &workflow)
{
    std::vector<const WorkflowExample *> out;
    for (const WorkflowExample &e : examples())
    {
        if (e.workflow == workflow)
        {
            out.push_back(&e);
        }
    }
    return out;
}

const WorkflowExample *findExample(const QString &id)
{
    for (const WorkflowExample &e : examples())
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

QString defaultReproductionsFolder()
{
    return QStringLiteral("D:/projects/reproduction-projects");
}

DataSet prepareExampleData(const DataRecipe &recipe, const QString &root)
{
    const QString path = QDir(root).filePath(recipe.file);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        throw std::runtime_error(QObject::tr("Cannot open %1.").arg(path).toStdString());
    }
    // Carriage returns alone end lines in some replication files.
    QString text = QString::fromUtf8(f.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty())
    {
        throw std::runtime_error(QObject::tr("%1 is empty.").arg(path).toStdString());
    }
    const auto unquote = [](QString s) { return s.trimmed().remove(QLatin1Char('"')); };
    QStringList header;
    for (const QString &h : lines.takeFirst().split(QLatin1Char(',')))
    {
        header << unquote(h);
    }
    const qsizetype dateAt = header.indexOf(recipe.dateColumn);
    if (dateAt < 0)
    {
        throw std::runtime_error(QObject::tr("%1 has no column %2.").arg(path, recipe.dateColumn).toStdString());
    }
    std::vector<qsizetype> at;
    for (const auto &[source, name] : recipe.columns)
    {
        const qsizetype i = header.indexOf(source);
        if (i < 0)
        {
            throw std::runtime_error(QObject::tr("%1 has no column %2.").arg(path, source).toStdString());
        }
        at.push_back(i);
    }
    DataSet d;
    d.source = path;
    for (const auto &[source, name] : recipe.columns)
    {
        d.names << name;
    }
    std::vector<std::vector<double>> rows;
    bool inSample = recipe.first.isEmpty();
    for (const QString &line : lines)
    {
        const QStringList cells = line.split(QLatin1Char(','));
        const QString label = convertDate(unquote(cells.value(dateAt)), recipe.dates);
        if (!inSample && label == recipe.first)
        {
            inSample = true;
        }
        if (!inSample)
        {
            continue;
        }
        std::vector<double> row;
        bool complete = true;
        for (const qsizetype i : at)
        {
            bool ok = false;
            const double v = unquote(cells.value(i)).toDouble(&ok);
            complete = complete && ok && std::isfinite(v);
            row.push_back(v * recipe.scale);
        }
        if (!complete)
        {
            if (recipe.dropIncomplete)
            {
                continue;
            }
            throw std::runtime_error(QObject::tr("%1: a value is missing in %2.").arg(path, label).toStdString());
        }
        d.periods << label;
        rows.push_back(row);
        if (!recipe.last.isEmpty() && label == recipe.last)
        {
            break;
        }
    }
    if (rows.empty())
    {
        throw std::runtime_error(QObject::tr("%1: no complete rows in the sample.").arg(path).toStdString());
    }
    d.values.set_size(rows.size(), recipe.columns.size());
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        for (std::size_t c = 0; c < recipe.columns.size(); ++c)
        {
            d.values(r, c) = rows[r][c];
        }
    }
    return d;
}

void writeDataSet(const DataSet &data, const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        throw std::runtime_error(QObject::tr("Cannot write %1.").arg(path).toStdString());
    }
    QTextStream out(&f);
    out << "date";
    for (const QString &n : data.names)
    {
        out << ',' << n;
    }
    out << '\n';
    for (int r = 0; r < data.length(); ++r)
    {
        out << data.periodLabel(r);
        for (int c = 0; c < data.series(); ++c)
        {
            out << ',' << QString::number(data.values(static_cast<arma::uword>(r), static_cast<arma::uword>(c)), 'g', 17);
        }
        out << '\n';
    }
}

} // namespace studio
