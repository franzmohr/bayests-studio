// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "engine/DataSet.h"
#include "engine/ModelRunner.h"
#include "engine/VarBuilder.h"
#include "views/StagePages.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QSplitter;
class QVBoxLayout;

namespace studio
{

class BuilderDialog;
class QtReporter;

/// The whole workflow side by side, for a wide screen -- the alternative to
/// the stage bar under View > Layout. Left to right:
///
///   modifications   the data, the specification and the chain, as the
///                   model builder has them, over small plots of the series;
///   priors          the prior hyperparameters and what BayesTS says about
///                   the model so specified, with the button that estimates it;
///   posterior       the draws of the selected model, the posterior stage's
///                   panels;
///   application     impulse responses above, forecasts below.
///
/// Estimating writes the specification into the project as a model of its
/// own, so every variant stays in the project to compare, and the right-hand
/// half then shows it.
class Workbench : public QWidget
{
    Q_OBJECT

public:
    Workbench(QtReporter *reporter, QWidget *parent = nullptr);

    /// Imported data: the left-hand panels specify a model on it, from the
    /// builder's defaults, until a model the builder wrote is selected.
    void setData(const DataSet &data);
    void setProjectFolder(const QString &folder) { projectDir_ = folder; }
    void showModel(const ModelContext &context);

    PosteriorPage *posterior() const { return posterior_; }

signals:
    void runRequested(const std::vector<studio::Stage> &stages);
    void importRequested();
    /// The builder wrote this model file: add it to the project and estimate.
    void modelSpecified(const QString &file);
    /// A model derived from the selected one (a sign and zero resample).
    void modelCreated(const QString &file);

private:
    /// The form on `data`, filled in from `spec` when there is one.
    void rebuildBuilder(const DataSet &data, const std::optional<VarBuilderSpec> &spec);
    void drawSeries();
    void specify();

    DataSet imported_;           ///< the data set imported last
    DataSet data_;               ///< the form's data: imported, or a model's own
    QString formModel_;          ///< the model whose record fills the form; empty for imported data
    QString selected_;           ///< the model selected last
    QString projectDir_;
    ModelContext context_;

    BuilderDialog *builder_ = nullptr;
    std::vector<QWidget *> sections_;   ///< the builder's, owned here once taken

    QVBoxLayout *modifications_;
    QVBoxLayout *priors_;
    QWidget *noData_;
    QLabel *source_;             ///< where the form's settings come from
    QLabel *priorsHint_;
    QVBoxLayout *seriesLayout_;
    QLabel *seriesNote_;

    class StateVarianceView *stateVariance_;
    PosteriorPage *posterior_;
    ApplicationPage *responses_;
    ApplicationPage *forecasts_;
};

} // namespace studio
