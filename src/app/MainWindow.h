// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QMainWindow>

#include <array>
#include <vector>

#include "engine/DataSet.h"
#include "engine/ModelRunner.h"
#include "views/AssistantPanel.h"
#include "views/StagePages.h"

class QAction;
class QDockWidget;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;

namespace studio
{

class ModelFileTree;
class Workbench;

/// The application window, laid out along the workflow of the bvartools
/// article: a project of models on the left, the five stages -- data, model
/// set-up, posterior simulation, evaluation, application -- as a bar beside
/// it, and the page of the selected stage in the middle.
class MainWindow : public QMainWindow, public AssistantHost
{
    Q_OBJECT

public:
    MainWindow();

    /// Opens a model file, or a folder whose model files form the project.
    void openPath(const QString &path);
    void runAll() { run({Stage::Coefficients, Stage::Forecast, Stage::LogLikelihood}); }
    void showStage(StageId stage);
    /// The workbench layout -- every stage's panels side by side, for a wide
    /// screen -- or the stage bar with one page at a time. `remember` makes it
    /// the layout the next start opens with: the menu's choice, not a switch
    /// a stage shortcut or the assistant makes in passing.
    void setWorkbench(bool on, bool remember = false);
    bool workbenchActive() const;

signals:
    void runEnded(bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildActions();
    void buildLayout();
    void selectModel(const QString &file);
    void refresh();
    void updateStageMarks();
    void run(const std::vector<Stage> &stages);
    void setRunning(bool running);
    void log(const QString &text);
    void showAbout();
    void showLicenceAgreement();
    void showLicences();
    void importData();

public:
    /// Imports a delimited file as the working data set.
    void importFrom(const QString &file);
    /// Adds a model file the builder wrote to the project and selects it.
    void addCreatedModel(const QString &file);
    /// Adds a model derived from the selected one -- a sign and zero
    /// identification's resample -- and opens its structural analysis.
    void addDerivedModel(const QString &file);
    const DataSet &workingData() const override { return data_; }

    // The assistant's host.
    QWidget *hostWindow() override { return this; }
    QString projectFolder() const override { return projectDir_; }
    QStringList projectModels() const override { return projectFiles_; }
    QString currentModel() const override { return file_; }
    void openProject(const QString &folder) override { openPath(folder); }
    void importDataFile(const QString &file) override { importFrom(file); }
    void chooseDataFile() override { importData(); }
    void openBuilder() override { newModel(); }
    void addModel(const QString &file) override { addCreatedModel(file); }
    void selectModelFile(const QString &file) override { selectModel(file); }
    void runAllStages() override { runAll(); }
    bool isRunning() const override;
    void showStageTab(WorkflowStage stage, const QString &tab) override;
    void openSignRestrictions(const std::vector<SignRestriction> &preset, int rotated) override;
    void reloadProject() override;
    AssistantPanel *assistant() const { return assistant_; }

private:
    void newModel();

    QString projectDir_;
    QStringList projectFiles_;
    QString file_;
    ModelRunner *runner_;
    ModelFileTree *tree_;
    CheckReport report_;
    DataSet data_;

    QListWidget *models_;
    QListWidget *stages_;
    QStackedWidget *pagesStack_;
    std::array<StagePage *, 5> pages_{};
    PosteriorPage *posterior_ = nullptr;
    QStackedWidget *layouts_ = nullptr;
    Workbench *workbench_ = nullptr;
    QAction *stagesLayout_ = nullptr;
    QAction *workbenchLayout_ = nullptr;
    /// The posterior page whose live bands follow the run in progress.
    PosteriorPage *liveTarget_ = nullptr;
    AssistantPanel *assistant_ = nullptr;
    QDockWidget *assistantDock_ = nullptr;
    QPlainTextEdit *log_;

    QLabel *stageLabel_;
    QProgressBar *progress_;
    QPushButton *cancel_;

    QAction *openFile_;
    QAction *openFolder_;
    QAction *reload_;
    QAction *runAll_;
    QAction *runCoefficients_;
    QAction *runForecast_;
    QAction *runLoglik_;
};

} // namespace studio
