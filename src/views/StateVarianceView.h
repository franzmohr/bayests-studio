// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QWidget>

#include <memory>
#include <vector>

#include "engine/StateVariance.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTemporaryDir;
class QwtPlot;

namespace studio
{

class BatchRunner;

/// A feeling for the state variance: the open time-varying model estimated
/// again with every coefficient's random walk variance at a level the user
/// names, each run's path of one coefficient drawn over the others' -- median
/// and 68% band -- with the estimated model's own as the reference. A small
/// variance flattens a path towards a constant coefficient; a large one lets
/// it follow the noise. The table beside it says how far the data moved each
/// state standard deviation from where its prior put it.
///
/// The runs are copies in a temporary folder, run as bayests processes, and
/// go when the model changes or the studio closes; the model file is never
/// touched.
class StateVarianceView : public QWidget
{
    Q_OBJECT

public:
    explicit StateVarianceView(QWidget *parent = nullptr);
    ~StateVarianceView() override;

    /// The model to explore, its estimated posterior shown as the reference
    /// when it has one. Empty, or a file without a state variance, disables it.
    void setModel(const QString &file, const QStringList &names);

private:
    struct Run
    {
        QString label;
        QString file;
        double variance = 0.0;
        bool fixed = false;
        bool reference = false;   ///< the model file's own posterior
        bool done = false;
        QString error;
    };

    void start(const std::vector<double> &variances);
    void batchFinished();
    void clearRuns();
    void redraw();
    void updateControls();

    QString file_;
    QStringList names_;
    StateVariancePrior prior_;
    std::vector<Run> runs_;
    std::unique_ptr<QTemporaryDir> folder_;
    int serial_ = 0;
    BatchRunner *runner_;

    QLabel *intro_;
    QLabel *status_;
    QComboBox *coefficient_;
    QLineEdit *variance_;
    QCheckBox *fixed_;
    QSpinBox *iterations_;
    QSpinBox *burnin_;
    QPushButton *run_;
    QPushButton *ladder_;
    QPushButton *clear_;
    QwtPlot *plot_;
    QTableWidget *table_;
};

} // namespace studio
