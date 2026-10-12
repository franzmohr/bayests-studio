// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <memory>

#include "engine/VolatilityBreak.h"

class QLabel;
class QTableWidget;
class QwtPlot;

namespace studio
{

/// The volatility break of Lenza and Primiceri (2022): the path of the scale
/// s_t from before the break to the end of the forecast horizon, median with
/// 68% and 90% bands; the posterior of the decay rate rho against its beta
/// prior; and a table of the free scales and rho.
class VolatilityBreakView : public QWidget
{
    Q_OBJECT

public:
    explicit VolatilityBreakView(QWidget *parent = nullptr);

    void setModel(const QString &file);

private:
    struct Result
    {
        QString error;
        std::shared_ptr<VolatilityBreakSummary> summary;
    };

    void show(const Result &result);
    void clear(const QString &message);

    QString file_;
    QFutureWatcher<Result> *watcher_;
    bool pending_ = false;
    QLabel *status_;
    QwtPlot *path_;
    QwtPlot *rho_;
    QTableWidget *table_;
};

} // namespace studio
