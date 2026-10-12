// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/Examples.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTextStream>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace studio
{

namespace
{

std::vector<WorkflowExample> make()
{
    std::vector<WorkflowExample> out;

    // -- Lütkepohl (2006), as the introduction of bvartools runs it: the
    // first example, its data shipped with the studio.
    {
        WorkflowExample e;
        e.id = QStringLiteral("luetkepohl2006");
        e.workflow = QStringLiteral("thesis");
        e.reproduction = false;   // a textbook's, listed with the introductory examples
        e.title = QObject::tr("Income and consumption: Lütkepohl (2006)");
        e.reference = QStringLiteral("Lütkepohl, H. (2006). New Introduction to Multiple Time Series Analysis, "
                                     "2nd ed. Berlin: Springer, data set E1; and the introduction of the R "
                                     "package bvartools (Mohr), whose model this example runs.");
        e.question = QObject::tr("How does consumption respond to a shock to disposable income, and how much of "
                                 "its forecast error variance do income shocks explain? A small textbook VAR -- the "
                                 "place to learn the workflow before a paper's.");
        // The data are shipped (engine/data), so there is no reproduction to locate.
        e.recipe.file = QStringLiteral(":/examples/luetkepohl-e1.csv");
        e.recipe.dateColumn = QStringLiteral("date");
        e.recipe.dates = DataRecipe::Dates::YearDotQuarter;
        e.recipe.columns = {{QStringLiteral("invest"), QStringLiteral("invest")},
                            {QStringLiteral("income"), QStringLiteral("income")},
                            {QStringLiteral("cons"), QStringLiteral("cons")}};
        e.recipe.transform = DataRecipe::Transform::LogDifference;
        e.recipe.scale = 100.0;
        e.recipe.last = QStringLiteral("1978 Q4");
        e.p = 2;
        e.coefPrecision = 0.0;   // bvartools' v_i = 0 and v_i_det = 0: flat
        e.detPrecision = 0.0;
        e.wishartScale = 1e-4;
        e.wishartDf = 1;
        e.iterations = 5000;
        e.burnin = 1000;
        e.seed = 1234567;
        e.forecastHorizon = 10;   // add_forecast_input(model, n_ahead = 10)
        e.notes = {
            {QStringLiteral("question"),
             QObject::tr("The data come with the studio: Lütkepohl's file E1, West German fixed investment, "
                         "disposable income and consumption expenditures, quarterly and seasonally adjusted, in "
                         "billions of DM, 1960Q1 to 1982Q4 (source: Deutsche Bundesbank). The assistant takes "
                         "100 × the log differences -- quarterly growth in percent -- and keeps 1960Q2 to "
                         "1978Q4, the 75 quarters the textbook estimates on; the rest is for forecast evaluation.")},
            {QStringLiteral("inspect"),
             QObject::tr("Growth rates, so the VAR is stationary as it stands. Investment growth swings by ten "
                         "points and more from quarter to quarter; income and consumption move by one or two, and "
                         "together.")},
            {QStringLiteral("baseline"),
             QObject::tr("Two lags and a constant, in the order invest, income, cons, as the textbook. The flat "
                         "prior is preselected, and the example sets bvartools' uninformative one -- zero precision on every "
                         "coefficient, and an inverse Wishart with one degree of freedom and a scale of 0.0001. "
                         "5,000 draws after 1,000, seed 1234567, and a forecast ten quarters ahead, 1979Q1 to "
                         "1981Q2, as the vignette's.")},
            {QStringLiteral("convergence"),
             QObject::tr("With a flat prior and two lags the sampler mixes quickly: the effective sample sizes are "
                         "close to the 5,000 draws, and the posterior means close to least squares.")},
            {QStringLiteral("lags"),
             QObject::tr("The textbook chooses two lags by the information criteria; compare them with one, three "
                         "and four by WAIC.")},
            {QStringLiteral("identify"),
             QObject::tr("Recursive, in the baseline's order: investment does not respond to income or consumption "
                         "shocks within the quarter, income not to consumption shocks. No sign restrictions.")},
            {QStringLiteral("responses"),
             QObject::tr("The response of cons to a shock to income, eight quarters, three ways: forecast error "
                         "(a unit shock to the income equation), orthogonalised (Cholesky) and generalised "
                         "(Pesaran and Shin 1998) -- the identification menu of the responses view. Then the "
                         "variance decomposition of cons.")},
        };
        e.findings = QObject::tr(
            "As bvartools finds: consumption rises by about 0.23 points in the quarter after a unit shock to income "
            "and by 0.26 in the second, and nothing is left of it after the third quarter. Orthogonalised, the "
            "response is about 0.49 on impact, 0.14 after one quarter and 0.36 after two (the generalised one is "
            "close: 0.53, 0.13, 0.40); the 95% bands exclude zero on impact and at two quarters. Income shocks "
            "explain about a quarter of the forecast error variance of consumption on impact and a third from two "
            "quarters on, investment shocks a tenth rising to a sixth, and consumption's own shocks the remaining "
            "two thirds falling to a half. The posterior means of "
            "the coefficients are close to least squares: 0.23 and 0.36 for the two lags of income in the "
            "consumption equation.");
        out.push_back(e);
    }

    // -- Lütkepohl (2006), E6: the VEC of the bvartools article, its data
    // shipped like E1's.
    {
        WorkflowExample e;
        e.id = QStringLiteral("luetkepohl2006vec");
        e.workflow = QStringLiteral("thesis");
        e.reproduction = false;   // a textbook's, with the introductory examples
        e.title = QObject::tr("Interest rate and inflation: Lütkepohl (2006)");
        e.reference = QStringLiteral("Lütkepohl, H. (2006). New Introduction to Multiple Time Series Analysis, "
                                     "2nd ed. Berlin: Springer, data set E6; and the VEC of the bvartools article "
                                     "(Mohr), whose model this example runs.");
        e.question = QObject::tr("Do the German long-term interest rate and inflation share a long-run relation, "
                                 "and how does inflation respond to an interest rate shock once that relation is "
                                 "accounted for? A vector error correction model for series in levels.");
        e.recipe.file = QStringLiteral(":/examples/luetkepohl-e6.csv");
        e.recipe.dateColumn = QStringLiteral("date");
        e.recipe.dates = DataRecipe::Dates::YearDotQuarter;
        e.recipe.columns = {{QStringLiteral("R"), QStringLiteral("R")}, {QStringLiteral("Dp"), QStringLiteral("Dp")}};
        e.recipe.scale = 100.0;
        e.vec = true;
        e.rank = 1;
        e.seasonal = true;
        e.p = 4;   // a level order: three lagged differences
        e.minnesota = false;
        e.coefPrecision = 1.0;   // bvartools' v_i = 1, v_i_det = 1/10
        e.detPrecision = 0.1;
        e.cointShrinkage = 0.1;  // coint v_i = 0.1, p_tau_i = 1, on the scaled term
        e.cointPTauInv = 1.0;
        e.cointScaleFromDifferences = true;
        e.wishartScale = 1.0;
        e.iterations = 10000;
        e.burnin = 2000;
        e.seed = 1234567;
        e.forecastHorizon = 8;
        e.notes = {
            {QStringLiteral("question"),
             QObject::tr("The data come with the studio: Lütkepohl's file E6, the German long-term interest rate "
                         "(Umlaufsrendite, from the Deutsche Bundesbank) and inflation, the quarterly change of the "
                         "log GDP deflator (from the DIW), 1972Q2 to 1998Q4, West Germany until 1990Q2 and all of "
                         "Germany after; times 100, as the bvartools article takes them. Inflation is not "
                         "seasonally adjusted.")},
            {QStringLiteral("inspect"),
             QObject::tr("Both wander without returning to a mean: the interest rate falls from the early 1980s, "
                         "and quarterly inflation swings with the seasons around a falling level. Series in levels "
                         "that may share a long-run relation are what a VEC is for.")},
            {QStringLiteral("baseline"),
             QObject::tr("The VEC is preselected: four lags of the levels, so three lagged differences, rank one, "
                         "an unrestricted constant and seasonal dummies, as the bvartools article specifies it. The "
                         "flat prior gives precision 1 to the coefficients and 0.1 to the deterministic terms, and "
                         "the cointegration space has the prior of Koop, León-González and Strachan (2010), "
                         "shrinkage 0.1, set as the article sets it on an error correction term scaled by the "
                         "standard deviation of each series' differences -- the studio writes the equivalent prior "
                         "on the unscaled term, so the forecast is in levels without rescaling. 10,000 draws after "
                         "2,000, seed 1234567, and a forecast eight quarters ahead.")},
            {QStringLiteral("convergence"),
             QObject::tr("The loadings and the cointegration vector are identified only together: read their "
                         "product, the long-run matrix, or the cointegration vector normalised on R.")},
            {QStringLiteral("lags"),
             QObject::tr("The article fixes four lags of the levels; compare three, five and six by WAIC.")},
            {QStringLiteral("identify"),
             QObject::tr("Recursive in the order R, Dp, or the forecast error responses the article plots. A VEC's "
                         "responses are those of the VAR in levels it implies, so a shock can have a permanent "
                         "effect.")},
            {QStringLiteral("responses"),
             QObject::tr("The forecast error response of Dp to R over 20 quarters, the article's figure: choose "
                         "Forecast error (feir) in the responses view and read the R column.")},
        };
        e.findings = QObject::tr(
            "As bvartools estimates the article's model: the cointegration vector normalised on R is about "
            "R - 4.2 Dp (the posterior mean of the ratio) -- quarterly inflation times four being annual inflation, "
            "a stationary real interest rate. A unit shock to the interest rate equation raises inflation by about "
            "0.21 after a quarter, and the effect settles at about 0.25 for good (the 95% band from about 0.07 to "
            "0.47 at 20 quarters) -- a permanent effect, which a VEC allows and a stationary VAR does not. "
            "Orthogonalised, R ordered first, the response is about -0.04 on impact and 0.13 at 20 quarters. The "
            "forecast keeps the interest rate near 3.9 and repeats inflation's seasonal swing, from about -2.2 to -2.5 in the "
            "first quarters to about 2.4 in the fourth.");
        out.push_back(e);
    }

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

    // -- Pinchetti (2026): geopolitical risk shocks from high-frequency
    // surprises inside a monthly VAR, two shocks told apart by oil.
    {
        WorkflowExample e;
        e.id = QStringLiteral("pinchetti2026");
        e.workflow = QStringLiteral("thesis");
        e.title = QObject::tr("Geopolitical risk and inflation: Pinchetti (2026)");
        e.reference = QStringLiteral("Pinchetti, M. (2026). Geopolitical Risk and Inflation: The Role of Energy "
                                     "Markets.");
        e.question = QObject::tr("Is geopolitical risk inflationary? The paper separates a macro geopolitical "
                                 "shock, which lowers oil prices, from an energy one, which raises them, and asks "
                                 "what each does to US prices and activity.");
        e.folder = QStringLiteral("2026-pinchetti");
        e.recipe.file = e.folder + QStringLiteral("/data/var/var_data_3day.csv");
        e.recipe.delimiter = QLatin1Char(';');
        e.recipe.dateColumn = QStringLiteral("year");
        e.recipe.monthColumn = QStringLiteral("month");
        e.recipe.dates = DataRecipe::Dates::YearAndMonth;
        e.recipe.columns = {{QStringLiteral("gpr_surprise"), QStringLiteral("gpr_surprise")},
                            {QStringLiteral("oil_surprise_spot"), QStringLiteral("oil_surprise_spot")},
                            {QStringLiteral("GPR"), QStringLiteral("GPR")},
                            {QStringLiteral("POIL"), QStringLiteral("POIL")},
                            {QStringLiteral("US_CPI"), QStringLiteral("US_CPI")},
                            {QStringLiteral("US_IP"), QStringLiteral("US_IP")},
                            {QStringLiteral("US_1Y"), QStringLiteral("US_1Y")}};
        e.recipe.first = QStringLiteral("1986-01");
        e.recipe.last = QStringLiteral("2019-12");
        e.p = 12;
        e.minnesota = true;
        // The paper's prior: standard deviation lambda l^-2 sigma_i / sigma_j
        // with lambda = 0.1, so kappa1 = lambda^2 and no extra cross-variable
        // shrinkage; sigma_i from AR(12)s; random walks for the four trending
        // series; a diffuse intercept; Wishart with k + 2 degrees of freedom
        // and scale diag(sigma_i^2).
        ExamplePrior prior;
        prior.kappa1 = 0.01;
        prior.kappa2 = 1.0;
        prior.lagDecay = 2.0;
        prior.autoregressions = true;
        prior.deterministicVariance = 1e10;
        prior.randomWalk = {QStringLiteral("GPR"), QStringLiteral("POIL"), QStringLiteral("US_CPI"),
                            QStringLiteral("US_IP")};
        prior.wishartScaleFromResiduals = true;
        prior.description = QObject::tr("The paper's Minnesota prior: λ = 0.1, lag decay l⁻², AR(12) scales");
        e.paperPrior = prior;
        e.wishartDf = 9;
        e.nIid = 2;
        e.iterations = 2000;
        e.burnin = 2000;
        e.thin = 4;
        e.seed = 12345;
        // Both shocks raise geopolitical risk for three months; oil tells them
        // apart on impact. Beyond 0.01 each, the paper's thresholds.
        const QString gs = QStringLiteral("gpr_surprise"), os = QStringLiteral("oil_surprise_spot"),
                      gpr = QStringLiteral("GPR");
        e.restrictions = {{gs, gs, 1, 0, 0.01},  {gs, os, -1, 0, 0.01}, {gs, gpr, 1, 0, 0.01},
                          {gs, gpr, 1, 1, 0.01}, {gs, gpr, 1, 2, 0.01}, {os, gs, 1, 0, 0.01},
                          {os, os, 1, 0, 0.01},  {os, gpr, 1, 0, 0.01}, {os, gpr, 1, 1, 0.01},
                          {os, gpr, 1, 2, 0.01}};
        e.signRotated = 2;
        e.forecastHorizon = 0;
        e.notes = {
            {QStringLiteral("question"),
             QObject::tr("The replication package's 3-day surprises (data/var/var_data_3day.csv): the change in "
                         "the geopolitical risk index and in the WTI spot price over three days around "
                         "geopolitical events, zero in the other months -- 30 of the 408 carry one -- and the "
                         "monthly geopolitical risk index, WTI, US CPI and industrial production, all 100 x log, "
                         "and the one-year yield in percent, 1986-01 to 2019-12. The 1-, 5- and 7-day windows "
                         "beside it are the paper's robustness checks.")},
            {QStringLiteral("inspect"),
             QObject::tr("The surprises are spikes on a line at zero; the other five trend, and stay in log "
                         "levels, as a Bayesian VAR in levels may (Sims, Stock and Watson 1990).")},
            {QStringLiteral("baseline"),
             QObject::tr("Twelve lags and a constant, the two surprises first. Their equations carry no "
                         "coefficients at all -- white noise, related to the rest only through the error "
                         "covariance, which is what identifies the shocks (Jarociński and Karadi 2020); the "
                         "example sets them so. The paper's prior is preselected: a Minnesota prior with "
                         "standard deviation 0.1 l⁻² sigma_i / sigma_j, the sigmas from AR(12)s, own first lags "
                         "centred on one for GPR, WTI, CPI and IP, a diffuse intercept, and a Wishart with 9 "
                         "degrees of freedom and scale diag(sigma_i^2). 2,000 draws after 2,000, every fourth "
                         "kept, seed 12345.")},
            {QStringLiteral("convergence"),
             QObject::tr("The surprise equations' coefficients are exactly zero in every draw -- a restriction, "
                         "not a tight prior.")},
            {QStringLiteral("lags"),
             QObject::tr("The paper fixes twelve lags, a year of monthly dynamics; the comparison is for "
                         "practice.")},
            {QStringLiteral("identify"),
             QObject::tr("Two shocks, from the two surprises: both raise the geopolitical risk surprise and the "
                         "index on impact and for two months after; the macro shock (the gpr_surprise column) "
                         "lowers the oil price surprise on impact, the energy shock (oil_surprise_spot) raises "
                         "it -- each by more than 0.01. Only those two shocks are rotated; the other five stay "
                         "the Cholesky factor's. The assistant fills in the restrictions, the threshold and the "
                         "rotated shocks; every draw should be identified.")},
            {QStringLiteral("responses"),
             QObject::tr("Choose Sign restrictions in the responses view and read the gpr_surprise and "
                         "oil_surprise_spot columns over 36 months: Figure 2 of the paper. The paper scales each "
                         "shock so that the median peak response of the index is one standard deviation of it "
                         "(33.8); the studio shows one-standard-deviation shocks, so multiply its macro "
                         "responses by about 4.2 and its energy responses by about 3.2 to read the paper's.")},
        };
        e.findings = QObject::tr(
            "As the bvartools reproduction finds, in the studio's units (the paper's scale in brackets): the macro "
            "shock raises the index by about 8.0 on impact (33.8), lowers WTI by about 1.5 (6.5) and by 2.4 "
            "(10.2) two months on, and with it consumer prices, about 0.05 (0.22) below by three years, and "
            "industrial production, 0.13 (0.54) at six months -- the 68% bands excluding zero; the one-year yield "
            "barely moves. The energy shock raises the index by about 10.7 (33.8), WTI by about 1.5 (4.6), and "
            "consumer prices with it, by about 0.03 (0.10) at seven months, the band excluding zero; the yield "
            "falls by about 0.03 (0.10) at eight months, and industrial production dips about two years on. So "
            "geopolitical risk is inflationary or disinflationary depending on which of the two moves -- the "
            "paper's point. Every draw admits a rotation. The random numbers are not MATLAB's, so the bands "
            "agree with the paper's in distribution, not draw by draw.");
        out.push_back(e);
    }

    // -- Jarociński and Karadi (2020): monetary policy and central bank
    // information shocks, the surprises missing before 1990 drawn every sweep.
    {
        WorkflowExample e;
        e.id = QStringLiteral("jk2020");
        e.workflow = QStringLiteral("thesis");
        e.title = QObject::tr("Monetary policy and information shocks: Jarociński and Karadi (2020)");
        e.reference = QStringLiteral("Jarociński, M. and Karadi, P. (2020). Deconstructing Monetary Policy "
                                     "Surprises -- The Role of Information Shocks. American Economic Journal: "
                                     "Macroeconomics 12(2), 1-43.");
        e.question = QObject::tr("An FOMC announcement that raises interest rates and lowers stock prices is a "
                                 "monetary tightening; one that raises both reveals the Fed's view of the economy. "
                                 "What does each do to output, prices and financial conditions -- and what does "
                                 "the standard identification, which ignores stocks, get wrong?");
        e.folder = QStringLiteral("2020-jarocinski karadi");
        e.recipe.file = e.folder + QStringLiteral("/data/data_var/data.csv");
        e.recipe.dateColumn = QStringLiteral("year");
        e.recipe.monthColumn = QStringLiteral("month");
        e.recipe.dates = DataRecipe::Dates::YearAndMonth;
        e.recipe.columns = {{QStringLiteral("ff4_hf"), QStringLiteral("ff4_hf")},
                            {QStringLiteral("sp500_hf"), QStringLiteral("sp500_hf")},
                            {QStringLiteral("gs1"), QStringLiteral("gs1")},
                            {QStringLiteral("logsp500"), QStringLiteral("logsp500")},
                            {QStringLiteral("us_rgdp"), QStringLiteral("us_rgdp")},
                            {QStringLiteral("us_gdpdef"), QStringLiteral("us_gdpdef")},
                            {QStringLiteral("ebpnew"), QStringLiteral("ebpnew")}};
        e.recipe.first = QStringLiteral("1984-02");
        e.recipe.last = QStringLiteral("2016-12");
        e.recipe.keepMissing = true;   // the surprises before 1990-02 and the S&P 500 one of 2001-09
        e.p = 12;
        e.minnesota = true;
        // The paper's prior: standard deviation 0.2 l^-1 sigma_i / sigma_j,
        // sigma_i from AR(12)s on each series with its gaps dropped, random
        // walks for the three series in log levels, a free constant; Wishart
        // with k + 2 degrees of freedom and scale diag(sigma_i^2).
        ExamplePrior prior;
        prior.kappa1 = 0.04;
        prior.kappa2 = 1.0;
        prior.lagDecay = 1.0;
        prior.autoregressions = true;
        prior.deterministicVariance = 1e10;
        prior.randomWalk = {QStringLiteral("logsp500"), QStringLiteral("us_rgdp"), QStringLiteral("us_gdpdef")};
        prior.wishartScaleFromResiduals = true;
        prior.description = QObject::tr("The paper's Minnesota prior: λ = 0.2, lag decay 1/l, AR(12) scales");
        e.paperPrior = prior;
        e.wishartDf = 9;
        e.nIid = 2;
        e.iterations = 2000;
        e.burnin = 2000;
        e.thin = 4;
        e.seed = 12345;
        // Signs on impact: monetary policy moves the surprises apart, central
        // bank information together. The other five shocks stay recursive.
        const QString ff = QStringLiteral("ff4_hf"), sp = QStringLiteral("sp500_hf");
        e.restrictions = {{ff, ff, 1, 0}, {ff, sp, -1, 0}, {sp, ff, 1, 0}, {sp, sp, 1, 0}};
        e.signRotated = 2;
        e.forecastHorizon = 0;
        e.notes = {
            {QStringLiteral("question"),
             QObject::tr("The replication package's data/data_var/data.csv: the surprises in the three-month fed "
                         "funds futures and the S&P 500 in the thirty minutes around FOMC announcements, in "
                         "percentage points; the one-year yield and the excess bond premium in percent; the S&P "
                         "500, real GDP and the GDP deflator in 100 x log, GDP interpolated to months by the "
                         "authors. 1984-02 to 2016-12. The surprises start in 1990-02, and the S&P 500 one is "
                         "missing in 2001-09: the assistant keeps those months, as the paper does, for the "
                         "sampler to draw.")},
            {QStringLiteral("inspect"),
             QObject::tr("The surprises are white noise around zero and blank before 1990; the other five trend "
                         "or wander, and stay in levels.")},
            {QStringLiteral("baseline"),
             QObject::tr("Twelve lags and a constant, the two surprises first and without coefficients -- white "
                         "noise reaching the macro variables only through the error covariance. The 121 "
                         "surprises not observed in the sample are drawn in every sweep from their distribution "
                         "given everything else; the twelve presample months are conditioned on at zero. The "
                         "paper's prior is preselected: a Minnesota prior with standard deviation 0.2 l⁻¹ "
                         "sigma_i / sigma_j, the sigmas from AR(12)s on each series with its gaps dropped, own "
                         "first lags centred on one for the S&P 500, GDP and the deflator, a free constant, and "
                         "a Wishart with 9 degrees of freedom and scale diag(sigma_i^2). 2,000 draws after 2,000, "
                         "every fourth kept, seed 12345.")},
            {QStringLiteral("convergence"),
             QObject::tr("The surprise equations' coefficients are exactly zero in every draw. A drawn surprise "
                         "has a posterior standard deviation close to that of the observed ones: the monthly "
                         "variables say little about a thirty-minute window, which is why the paper can keep "
                         "those months without the gaps steering the results.")},
            {QStringLiteral("lags"),
             QObject::tr("The paper fixes twelve lags; the comparison is for practice.")},
            {QStringLiteral("identify"),
             QObject::tr("On impact, a monetary policy shock (the ff4_hf column) raises the fed funds futures and "
                         "lowers stocks; a central bank information shock (sp500_hf) raises both. Only those two "
                         "shocks are rotated; the other five stay the Cholesky factor's. The assistant fills in "
                         "the signs and the rotated shocks; every draw admits a rotation.")},
            {QStringLiteral("responses"),
             QObject::tr("Figure 2: choose Sign restrictions, 36 months, and read the ff4_hf and sp500_hf columns "
                         "for the five macro variables; panel B, the standard high-frequency identification, is "
                         "the Orthogonalised (oir) ff4_hf column of the same model. Table 2 is the impact "
                         "responses of the two surprises, in percentage points -- times 100 for the paper's "
                         "basis points. Figure 4 is the historical decomposition of ff4_hf under the sign "
                         "restrictions, from 1990.")},
        };
        e.findings = QObject::tr(
            "Table 2 of the paper: on impact the monetary policy shock raises the fed funds futures by 5 basis "
            "points (3 to 6) and lowers the S&P 500 by 42 (52 to 23); the information shock raises them by 3 (0 to "
            "5) and 28 (3 to 45); the standard identification gives 6 and -21. The bvartools reproduction is within "
            "two basis points of each. A monetary policy shock lowers real GDP and the deflator persistently, by "
            "about 0.08 and 0.05 percent after two years, and raises the excess bond premium; an information "
            "shock raises the one-year yield by about 0.10 a year on, against 0.03 after the policy shock, lowers "
            "the bond premium and raises output and prices. The standard identification mixes the two: its "
            "deflator response, about -0.03 after two years, is the weak one the literature had puzzled over -- "
            "the paper's result. The random numbers are not MATLAB's, so the bands agree in distribution, not draw "
            "by draw.");
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

bool isBuiltinData(const DataRecipe &recipe)
{
    return recipe.file.startsWith(QLatin1String(":/"));
}

DataSet prepareExampleData(const DataRecipe &recipe, const QString &root)
{
    const QString path = isBuiltinData(recipe) ? recipe.file : QDir(root).filePath(recipe.file);
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
    for (const QString &h : lines.takeFirst().split(recipe.delimiter))
    {
        header << unquote(h);
    }
    const qsizetype dateAt = header.indexOf(recipe.dateColumn);
    if (dateAt < 0)
    {
        throw std::runtime_error(QObject::tr("%1 has no column %2.").arg(path, recipe.dateColumn).toStdString());
    }
    const bool yearAndMonth = recipe.dates == DataRecipe::Dates::YearAndMonth;
    const qsizetype monthAt = yearAndMonth ? header.indexOf(recipe.monthColumn) : -1;
    if (yearAndMonth && monthAt < 0)
    {
        throw std::runtime_error(QObject::tr("%1 has no column %2.").arg(path, recipe.monthColumn).toStdString());
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
        const QStringList cells = line.split(recipe.delimiter);
        // "1986-01", as an ISO date's month is labelled.
        const QString label = yearAndMonth ? QStringLiteral("%1-%2")
                                                 .arg(unquote(cells.value(dateAt)))
                                                 .arg(unquote(cells.value(monthAt)).toInt(), 2, 10, QLatin1Char('0'))
                                           : convertDate(unquote(cells.value(dateAt)), recipe.dates);
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
            if (recipe.keepMissing && !ok)
            {
                row.push_back(std::numeric_limits<double>::quiet_NaN());
                continue;
            }
            complete = complete && ok && std::isfinite(v);
            row.push_back(v);
        }
        if (!complete)
        {
            // A dropped row in a differenced series would make one difference
            // span two periods.
            if (recipe.dropIncomplete && recipe.transform == DataRecipe::Transform::None)
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
    if (recipe.transform == DataRecipe::Transform::LogDifference)
    {
        for (std::size_t r = 0; r < rows.size(); ++r)
        {
            for (std::size_t c = 0; c < recipe.columns.size(); ++c)
            {
                if (rows[r][c] <= 0.0)
                {
                    throw std::runtime_error(QObject::tr("%1: %2 is not positive in %3, so it has no log.")
                                                 .arg(path, recipe.columns[c].first, d.periods.at(static_cast<qsizetype>(r)))
                                                 .toStdString());
                }
            }
        }
        for (std::size_t r = rows.size(); r-- > 1;)
        {
            for (std::size_t c = 0; c < recipe.columns.size(); ++c)
            {
                rows[r][c] = std::log(rows[r][c]) - std::log(rows[r - 1][c]);
            }
        }
        if (!rows.empty())
        {
            rows.erase(rows.begin());
            d.periods.removeFirst();
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
            d.values(r, c) = rows[r][c] * recipe.scale;
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
            // A value not observed is an empty cell, which the reader takes back as one.
            const double v = data.values(static_cast<arma::uword>(r), static_cast<arma::uword>(c));
            out << ',';
            if (std::isfinite(v))
            {
                out << QString::number(v, 'g', 17);
            }
        }
        out << '\n';
    }
}

} // namespace studio
