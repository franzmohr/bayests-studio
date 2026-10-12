// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QObject>
#include <QStringList>

#include <vector>

class QProcess;

namespace studio
{

/// Runs `bayests posterior` over many model files -- a horse race's windows
/// -- as separate processes, several at once. A process of its own per model
/// keeps the samplers off the application's threads, where the HDF5 library,
/// built without thread safety, could not have them, and puts every core to
/// work; each is single-threaded (OMP_NUM_THREADS = 1), so its draws are
/// reproducible from the file's seed. A file whose stages are in it already
/// is skipped by bayests itself, so a stopped run resumes where it stopped.
class BatchRunner : public QObject
{
    Q_OBJECT

public:
    explicit BatchRunner(QObject *parent = nullptr);
    ~BatchRunner() override;

    /// The bayests executable: beside the application, or STUDIO_BAYESTS.
    static QString executable();

    void start(const QStringList &files, int parallel = 0);
    void cancel();
    bool isRunning() const { return running_; }

signals:
    void progress(int done, int total, const QString &file);
    void failed(const QString &file, const QString &message);
    void finished(int succeeded, int failedCount, bool cancelled);

private:
    void launchNext();

    QStringList queue_;
    std::vector<QProcess *> active_;
    int total_ = 0, done_ = 0, failed_ = 0, parallel_ = 1;
    bool running_ = false, cancelled_ = false;
};

} // namespace studio
