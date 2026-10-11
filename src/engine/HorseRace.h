#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <vector>

#include "bayests/arma.h"
#include "engine/DataSet.h"
#include "engine/VarBuilder.h"

namespace studio
{

/// A forecast horse race: competing models estimated over an expanding window
/// -- every one from the same first period to each forecast origin in turn,
/// as bvartools' use_expanding_window() sets them up -- each forecasting the
/// periods after its origin, which are held out and scored.
struct HorseRaceCandidate
{
    QString name;               ///< "BVAR, Minnesota prior"
    QString description;
    VarBuilderSpec spec;        ///< everything but the data, the sample and the horizon
};

struct HorseRaceDesign
{
    DataSet data;
    std::vector<int> columns;   ///< the variables, in the models' order
    int firstRow = 0;           ///< where every window starts
    int firstOrigin = 0;        ///< data-set rows of the last estimation period of the first window
    int lastOrigin = 0;         ///< ... and of the last; at most the second to last row, so it is scored
    int step = 1;               ///< periods between origins
    int horizon = 8;
    std::vector<HorseRaceCandidate> candidates;
    int benchmark = 0;          ///< the candidate the others are measured against
    int iterations = 5000;
    int burnin = 1000;
    std::uint64_t seed = 20261011ULL;

    std::vector<int> origins() const;
};

/// A refusal of the design, or empty: what the form shows before any file is
/// written.
QString horseRaceRefusal(const HorseRaceDesign &design);

/// The standard field of a central bank horse race, for the variables chosen:
/// a BVAR under the Minnesota prior (the benchmark), the same with stochastic
/// volatility, a TVP-VAR with stochastic volatility, a BVAR with Bayesian
/// variable selection, a parsimonious BVAR of one lag, and -- for series in
/// levels -- a VEC of rank one. `lags` is the lag order of the larger VARs,
/// four for quarterly data by default.
std::vector<HorseRaceCandidate> standardCandidates(int lags, bool levels);

/// The lag order a frequency suggests: 4 for quarterly, 12 for monthly, 2 for
/// annual data, 4 otherwise.
int suggestedLags(const DataSet &data);

/// What a horse race wrote: its manifest, horse-race.json in `folder`.
struct HorseRaceManifest
{
    QString folder;
    QStringList candidates;
    QStringList variables;
    QStringList origins;                       ///< period labels
    std::vector<double> originTimes;           ///< the time of each origin, for plots
    int benchmark = 0;
    int horizon = 0;
    std::vector<std::vector<QString>> files;   ///< [candidate][origin]

    int models() const;
};

/// Writes one model file per candidate and origin under `folder`
/// (<candidate>/<origin>.h5) and the manifest. Every window starts at
/// `firstRow`; the periods after its origin, up to the horizon, are held out
/// and scored. `progress(done, total)` may cancel by returning false.
HorseRaceManifest writeHorseRace(const HorseRaceDesign &design, const QString &folder,
                                 const std::function<bool(int, int)> &progress = {});

/// Reads horse-race.json. Throws when there is none.
HorseRaceManifest readHorseRace(const QString &folder);

/// The scores of every candidate, from the files once they have been run.
///
/// RMSE: the root mean squared error of the posterior mean forecast against
/// what the period realised, over every origin that observed it, per variable
/// and horizon -- the point forecast's accuracy.
///
/// Log score: per origin, the log predictive density of what the held-out
/// periods realised, sum over horizons of log mean_d p(y_{T+h} | draw d), as
/// BayesTS writes it in /posterior/forecast/loglik -- each period conditioning
/// on the realised ones before it, so the sum is the joint density of the path.
/// Summed over origins it is the cumulative log score, whose difference from
/// the benchmark is a log Bayes factor of the forecasts.
struct HorseRaceScores
{
    HorseRaceManifest manifest;
    std::vector<arma::mat> rmse;        ///< [candidate] k x h, NaN where no origin observed the horizon
    std::vector<arma::mat> observed;    ///< [candidate] k x h, origins that did
    std::vector<arma::vec> logScore;    ///< [candidate] per origin, NaN where not run or not scored
    std::vector<int> complete;          ///< [candidate] origins whose file holds a forecast
};

HorseRaceScores scoreHorseRace(const HorseRaceManifest &manifest);

} // namespace studio
