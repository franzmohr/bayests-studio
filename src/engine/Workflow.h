// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

#include "engine/VarBuilder.h"

namespace studio
{

/// The stages of the studio, as the workflows refer to them: the order of the
/// stage bar (views/StagePages.h's StageId), kept here so the engine does not
/// depend on the views.
enum class WorkflowStage
{
    Data,
    Setup,
    Posterior,
    Evaluation,
    Application,
};

/// What a workflow needs to know of one model file.
struct ModelFacts
{
    QString file;
    QString algorithm;
    bool ok = false;
    bool estimated = false;     ///< posterior draws
    bool loglik = false;        ///< /posterior/loglik, for WAIC
    bool scored = false;        ///< a forecast scored against held-out data
    bool sign = false;          ///< a sign identification
    int p = 0;
};

ModelFacts modelFacts(const QString &file);

/// The project as a workflow sees it: the data, the models, and what the
/// user has confirmed or the assistant has done, from assistant.json.
struct ProjectState
{
    QString folder;
    bool hasData = false;
    QString currentModel;
    std::vector<ModelFacts> models;
    QSet<QString> confirmed;            ///< steps the user ticked off
    QSet<QString> events;               ///< what the assistant did: "exported", "variants"
    QStringList baselineModels;         ///< files the assistant created as baseline and variants
    bool horseRace = false;             ///< a manifest in <folder>/horse-race
    int horseRaceModels = 0;
    int horseRaceRun = 0;               ///< of them with a forecast

    const ModelFacts *model(const QString &file) const;
    int estimatedModels() const;
    int modelsWithLoglik() const;
};

/// One action a step offers; the assistant panel maps the id to what it does.
struct WorkflowAction
{
    QString id;
    QString label;
    QString tip;
};

struct WorkflowStep
{
    QString id;
    QString title;
    QString summary;                    ///< one line, in the step list
    QString guidance;                   ///< HTML: what to do, why, and what the literature does
    WorkflowStage stage = WorkflowStage::Data;
    QString tab;                        ///< the tab of the stage to show, by its title; empty for none
    bool optional = false;
    /// Done when the project shows it -- or, where `confirm` is set, when the
    /// user has ticked that statement, the step being a judgement only they
    /// can make.
    std::function<bool(const ProjectState &)> done;
    QString confirm;
    std::vector<WorkflowAction> actions;
};

struct Workflow
{
    QString id;
    QString title;
    QString audience;
    QString summary;
    std::vector<WorkflowStep> steps;
};

/// The master thesis workflow and the forecast horse race.
const std::vector<Workflow> &workflows();
const Workflow *findWorkflow(const QString &id);

bool stepDone(const WorkflowStep &step, const ProjectState &state);

/// The step to work on: the first one not done that is not optional, or the
/// last step when everything is.
int currentStep(const Workflow &workflow, const ProjectState &state);

/// The assistant's record in a project, assistant.json: the workflow chosen,
/// what was confirmed and done, and the baseline specification the variants
/// are made from.
struct AssistantRecord
{
    QString workflow;
    QString example;                    ///< a worked example's id, or empty
    QString dataFile;                   ///< the data set the assistant imported, relative to the folder
    QSet<QString> confirmed;
    QSet<QString> events;
    QStringList models;                 ///< relative to the project folder
    QJsonObject baseline;               ///< specToJson() of the baseline
};

AssistantRecord readAssistantRecord(const QString &folder);
void writeAssistantRecord(const QString &folder, const AssistantRecord &record);

/// A builder specification without its data, the columns and sample by name
/// and period label so it applies to the same data set imported again.
QJsonObject specToJson(const VarBuilderSpec &spec);
std::optional<VarBuilderSpec> specFromJson(const QJsonObject &json, const DataSet &data);

/// The robustness checks a thesis reports, as variants of its baseline: the
/// lag order either side, the reverse recursive ordering, a looser and a
/// tighter prior, and the sample without its first fifth. Names are file
/// stems.
std::vector<std::pair<QString, VarBuilderSpec>> lagVariants(const VarBuilderSpec &baseline);
std::vector<std::pair<QString, VarBuilderSpec>> robustnessVariants(const VarBuilderSpec &baseline);

/// A draft of the methods paragraph of a thesis, in LaTeX, from the model
/// file and, where the assistant made it, its specification: the model, the
/// sample, the prior, the sampler, the identification -- with the references
/// a reader expects.
QString methodsParagraph(const QString &file, const std::optional<VarBuilderSpec> &spec);

} // namespace studio
