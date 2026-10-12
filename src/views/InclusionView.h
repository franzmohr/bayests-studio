// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <memory>
#include <vector>

#include "engine/CoefficientSummary.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace studio
{

/// The posterior inclusion probabilities of a model estimated with variable
/// selection (BVS or SSVS): one table per block of coefficients, a row per
/// equation and a column per regressor, as bvartools' summary() reports
/// them, and the covariance block's where it was selected over too. A
/// probability of one half or more is set in bold -- the median probability
/// model of Barbieri and Berger (2004) -- and an element selection did not
/// reach is greyed out.
class InclusionView : public QWidget
{
    Q_OBJECT

public:
    explicit InclusionView(QWidget *parent = nullptr);

    void setModel(const QString &file);

private:
    struct Result
    {
        QString error;
        std::shared_ptr<CoefficientSummary> summary;
    };

    void show(const Result &result);
    void clearTables();

    QString file_;
    QFutureWatcher<Result> *watcher_;
    bool pending_ = false;
    QLabel *status_;
    QPushButton *exportAll_;
    std::vector<QTableWidget *> exported_;
    QWidget *host_;
    QVBoxLayout *tables_;
};

} // namespace studio
