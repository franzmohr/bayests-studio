#include "engine/ModelRunner.h"
#include "engine/ModelFileTree.h"

#include "engine/QtReporter.h"

#include <QThread>

#include <exception>
#include <optional>
#include <string>

// BayesTS internals the command line is built from. The studio is a host of
// the model-file layer, not only of the samplers, so it reaches past
// include/bayests/ on purpose -- which is why BayesTS is pinned to a commit.
#include "io/hdf5/hdf5_and_armadillo.h"
#include "io/hdf5/model_io_common.h"
#include "model_seed.h"
#include "models/models.h"

namespace studio
{

QString stageName(Stage stage)
{
    switch (stage)
    {
    case Stage::Coefficients: return QObject::tr("Posterior simulation");
    case Stage::Forecast: return QObject::tr("Forecast");
    case Stage::LogLikelihood: return QObject::tr("Log likelihood");
    }
    return {};
}

namespace
{

struct Located
{
    std::string algorithm;
    std::optional<std::uint64_t> seed;
};

Located locate(const ModelLocation &location)
{
    // Opened and closed before the front-end opens the file read-write.
    HighFive::File h5 = open_hdf5_file(location.file);
    require_group(h5, location.group);
    const ModelFile file(h5, location.group);
    return {get_algorithm_type(file), bayests::hdf5_io::read_model_seed(file)};
}

ModelLocation toLocation(const QString &file, const QString &group)
{
    return {std::filesystem::path(file.toStdWString()), group.toStdString()};
}

} // namespace

ModelRunner::ModelRunner(QObject *parent)
    : QObject(parent), reporter_(new QtReporter)
{
    qRegisterMetaType<studio::Stage>();
    // The reporter lives without a parent: it is moved to each worker thread's
    // lifetime implicitly by being used there, and its signals are queued to
    // this object's thread because the connection type is decided per emit.
    connect(reporter_, &QtReporter::progressChanged, this, &ModelRunner::progressChanged,
            Qt::QueuedConnection);
    connect(reporter_, &QtReporter::messageEmitted, this, &ModelRunner::messageEmitted,
            Qt::QueuedConnection);
}

ModelRunner::~ModelRunner()
{
    reporter_->request_cancel();
    // A running worker holds a pointer to the reporter; it is deleted only
    // once no worker is left (see start()).
    if (!running_.load())
    {
        delete reporter_;
    }
}

CheckReport ModelRunner::check(const QString &file, const QString &group)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    CheckReport report;
    try
    {
        const ModelLocation location = toLocation(file, group);
        const Located found = locate(location);
        report.algorithm = QString::fromStdString(found.algorithm);

        const ModelCheck result = create_model(found.algorithm)->check(location);
        const bayests::VarSpec &spec = result.spec;

        report.ok = true;
        report.hasPosterior = result.has_posterior;
        report.periods = static_cast<int>(result.periods);
        report.k = spec.k;
        report.p = spec.p;
        report.rank = spec.rank;
        report.kBeta = spec.k_beta;
        report.structural = spec.structural;
        report.errorSpec = QString::fromStdString(result.error_attribute);
        report.factors = spec.n_factors;
        report.horizon = spec.h;
        report.iterations = spec.iterations;
        report.burnin = spec.burnin;
        report.thin = spec.thin;
        if (result.seed)
        {
            report.seed = QString::number(static_cast<qulonglong>(*result.seed));
        }
        // The samplers that report kept draws of a path; the discounted pair
        // write a posterior, not draws.
        report.timeVarying = found.algorithm.find("Tvp") != std::string::npos &&
                             found.algorithm.find("Discount") == std::string::npos;
        report.facts << tr("Periods: %1").arg(result.periods)
                     << tr("Endogenous variables (k): %1").arg(spec.k)
                     << tr("Lags (p): %1").arg(spec.p)
                     << tr("Draws kept: %1, burn-in %2, thinning %3")
                            .arg(spec.iterations).arg(spec.burnin).arg(spec.thin)
                     << tr("Forecast horizon: %1").arg(spec.h);
        if (result.seed)
        {
            report.facts << tr("Seed: %1").arg(static_cast<qulonglong>(*result.seed));
        }
        if (result.posterior_interrupted)
        {
            report.warnings << tr("A previous run stopped while writing draws; "
                                  "they will be estimated again.");
        }
        for (const auto &name : result.unread)
        {
            report.warnings << tr("Dataset %1 is not read by %2.")
                                   .arg(QString::fromStdString(name), report.algorithm);
        }
        for (const auto &name : result.unknown_attributes)
        {
            report.warnings << tr("/model attribute '%1' is read by no model.")
                                   .arg(QString::fromStdString(name));
        }
        for (const auto &flat : result.flat_selection)
        {
            report.warnings << QString::fromStdString(
                bayests::flat_selection_message(flat.report, flat.block));
        }
    }
    catch (const std::exception &e)
    {
        report.ok = false;
        report.error = QString::fromUtf8(e.what());
    }
    return report;
}

void ModelRunner::start(const QString &file, const QString &group, const std::vector<Stage> &stages)
{
    if (running_.exchange(true))
    {
        return;
    }
    reporter_->reset();

    QtReporter *reporter = reporter_;
    QThread *thread = QThread::create([this, reporter, file, group, stages]() {
        // The HDF5 library is built without thread safety: while the sampler
        // reads and writes the file, nothing else may be inside it. A reader on
        // the interface's thread waits here rather than corrupting its state.
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        bool ok = true;
        bool cancelled = false;
        QString error;
        try
        {
            const ModelLocation location = toLocation(file, group);
            const Located found = locate(location);

            for (const Stage stage : stages)
            {
                QMetaObject::invokeMethod(this, [this, stage]() { emit stageStarted(stage); },
                                          Qt::QueuedConnection);

                // A fresh front-end per stage, as the command line creates one
                // per subcommand.
                auto model = create_model(found.algorithm);
                model->set_reporter(reporter);
                switch (stage)
                {
                case Stage::Coefficients:
                    seed_model_stage(found.seed, ModelStage::coefficients);
                    model->draw_coefficients(location);
                    break;
                case Stage::Forecast:
                    seed_model_stage(found.seed, ModelStage::forecast);
                    model->forecast(location);
                    break;
                case Stage::LogLikelihood:
                    seed_model_stage(found.seed, ModelStage::log_likelihood);
                    model->log_likelihood(location);
                    break;
                }
            }
        }
        catch (const RunCancelled &)
        {
            ok = false;
            cancelled = true;
        }
        catch (const std::exception &e)
        {
            ok = false;
            error = QString::fromUtf8(e.what());
        }

        QMetaObject::invokeMethod(this, [this, ok, cancelled, error]() {
            running_.store(false);
            emit runFinished(ok, cancelled, error);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ModelRunner::cancel()
{
    reporter_->request_cancel();
}

} // namespace studio
