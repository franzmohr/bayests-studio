// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QObject>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "bayests/reporter.h"

namespace studio
{

/// Thrown from check_interrupt() when the user cancels. Derived from
/// std::runtime_error so a front-end that only catches std::exception still
/// unwinds cleanly; ModelRunner tells it apart from a genuine failure.
class RunCancelled : public std::runtime_error
{
public:
    RunCancelled() : std::runtime_error("Cancelled by the user") {}
};

/// The BayesTS Reporter a run on the worker thread talks to.
///
/// The samplers call progress() once per draw and leave throttling to the
/// host; this emits at most every 50 ms, plus the final draw, so the GUI
/// thread is not flooded by queued signals. Signals cross to the GUI thread
/// through Qt's queued connections.
class QtReporter final : public QObject, public bayests::Reporter
{
    Q_OBJECT

public:
    explicit QtReporter(QObject *parent = nullptr) : QObject(parent) {}

    void request_cancel() { cancel_.store(true); }
    void reset();

    /// Which row of the time-varying block "a" to collect kept draws of, as
    /// they are made; -1 collects nothing. Safe to change during a run: the
    /// draws collected so far are dropped, since they belong to another row.
    void watch(int row);
    int watched() const { return watch_.load(); }

    /// Rows of the last kept draw of "a" seen in this run: the number of
    /// coefficients per period. 0 until the first one.
    int pathRows() const { return rows_.load(); }

    /// Moves the draws collected since the last call out of the reporter.
    /// Each is one path, one value per period.
    std::vector<std::vector<double>> takeDraws();

    void message(const std::string &text) override;
    void progress(long long done, long long total) override;
    void finish() override;
    void check_interrupt() override;
    void kept_draw(const std::string &block, long long index, const arma::mat &value) override;

signals:
    void messageEmitted(const QString &text);
    void progressChanged(qint64 done, qint64 total);
    void finished();

private:
    std::atomic<bool> cancel_{false};
    std::chrono::steady_clock::time_point last_{};

    std::atomic<int> watch_{-1};
    std::atomic<int> rows_{0};
    std::mutex mutex_;
    std::vector<std::vector<double>> pending_;
};

} // namespace studio
