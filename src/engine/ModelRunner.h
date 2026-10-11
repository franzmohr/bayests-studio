#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <vector>

namespace studio
{

class QtReporter;

/// The three stages of a model file, in the order a full run takes them.
enum class Stage
{
    Coefficients,
    Forecast,
    LogLikelihood,
};

QString stageName(Stage stage);

/// What `bayests check` reports about one model, flattened for display.
struct CheckReport
{
    bool ok = false;
    QString algorithm;
    QString error;            ///< the refusal, when ok is false
    QStringList facts;        ///< resolved dimensions and switches
    QStringList warnings;     ///< unread datasets, unknown attributes, flat priors
    bool hasPosterior = false;
    int periods = 0;
    int k = 0;                ///< endogenous variables
    int p = 0;                ///< lags (a level order for a VEC)
    int rank = 0;             ///< a VEC's cointegration rank, 0 otherwise
    int kBeta = 0;            ///< rows of a VEC's beta
    bool structural = false;
    int factors = 0;          ///< unobserved factors, 0 for a VAR or VEC
    int horizon = 0;          ///< forecast horizon, 0 for none
    int iterations = 0, burnin = 0, thin = 1;
    QString seed;             ///< empty when the file names none
    bool timeVarying = false; ///< samples a coefficient path (block "a" per period)
};

/// Runs BayesTS model stages in-process, on a thread of its own.
///
/// Each stage does what the command line does for one model -- read the
/// algorithm and /model/seed, seed the RNG for the stage, hand the location to
/// the registered front-end -- except that progress and cancellation go through
/// a QtReporter instead of stdout. Seeding happens on the worker thread because
/// Armadillo's generator is thread-local.
///
/// Live in the GUI thread; call start() and listen. One run at a time.
class ModelRunner : public QObject
{
    Q_OBJECT

public:
    explicit ModelRunner(QObject *parent = nullptr);
    ~ModelRunner() override;

    bool isRunning() const { return running_.load(); }

    /// The reporter runs talk to, for views that follow a run live.
    QtReporter *reporter() const { return reporter_; }

    /// Reads and validates without drawing anything. Synchronous: it is fast,
    /// and the GUI wants the answer before enabling the run buttons.
    static CheckReport check(const QString &file, const QString &group);

public slots:
    void start(const QString &file, const QString &group, const std::vector<Stage> &stages);
    void cancel();

signals:
    void stageStarted(studio::Stage stage);
    void progressChanged(qint64 done, qint64 total);
    void messageEmitted(const QString &text);
    /// ok is false on failure and on cancellation; cancelled tells them apart.
    void runFinished(bool ok, bool cancelled, const QString &error);

private:
    QtReporter *reporter_;
    std::atomic<bool> running_{false};
};

} // namespace studio

Q_DECLARE_METATYPE(studio::Stage)
