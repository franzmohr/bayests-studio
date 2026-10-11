#include "engine/BatchRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include <algorithm>

namespace studio
{

BatchRunner::BatchRunner(QObject *parent) : QObject(parent) {}

BatchRunner::~BatchRunner()
{
    cancel();
    for (QProcess *p : active_)
    {
        p->waitForFinished(5000);
    }
}

QString BatchRunner::executable()
{
    const QByteArray env = qgetenv("STUDIO_BAYESTS");
    if (!env.isEmpty())
    {
        return QString::fromLocal8Bit(env);
    }
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("bayests.exe");
#else
    const QString name = QStringLiteral("bayests");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

void BatchRunner::start(const QStringList &files, int parallel)
{
    if (running_)
    {
        return;
    }
    queue_ = files;
    total_ = static_cast<int>(files.size());
    done_ = failed_ = 0;
    cancelled_ = false;
    // One core left for the application.
    parallel_ = parallel > 0 ? parallel : std::max(1, QThread::idealThreadCount() - 1);
    running_ = true;
    if (queue_.isEmpty())
    {
        running_ = false;
        emit finished(0, 0, false);
        return;
    }
    for (int i = 0; i < parallel_ && !queue_.isEmpty(); ++i)
    {
        launchNext();
    }
}

void BatchRunner::cancel()
{
    if (!running_)
    {
        return;
    }
    cancelled_ = true;
    queue_.clear();
    for (QProcess *p : active_)
    {
        p->kill();
    }
}

void BatchRunner::launchNext()
{
    if (queue_.isEmpty())
    {
        return;
    }
    const QString file = queue_.takeFirst();
    auto *process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::MergedChannels);
    active_.push_back(process);
    connect(process, &QProcess::finished, this, [this, process, file](int code, QProcess::ExitStatus status) {
        active_.erase(std::remove(active_.begin(), active_.end(), process), active_.end());
        ++done_;
        if (!cancelled_ && (status != QProcess::NormalExit || code != 0))
        {
            ++failed_;
            QString message = QString::fromLocal8Bit(process->readAll()).trimmed();
            emit failed(file, message.right(600));
        }
        process->deleteLater();
        emit progress(done_, total_, file);
        if (!queue_.isEmpty())
        {
            launchNext();
        }
        else if (active_.empty())
        {
            running_ = false;
            emit finished(done_ - failed_, failed_, cancelled_);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, file](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            active_.erase(std::remove(active_.begin(), active_.end(), process), active_.end());
            ++done_;
            ++failed_;
            emit failed(file, tr("Cannot start %1.").arg(executable()));
            process->deleteLater();
            emit progress(done_, total_, file);
            if (active_.empty())
            {
                queue_.clear();
                running_ = false;
                emit finished(done_ - failed_, failed_, cancelled_);
            }
        }
    });
    process->start(executable(), {QStringLiteral("posterior"), file});
}

} // namespace studio
