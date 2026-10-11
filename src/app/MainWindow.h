#pragma once

#include <QMainWindow>

#include <array>
#include <vector>

#include "engine/DataSet.h"
#include "engine/ModelRunner.h"
#include "views/StagePages.h"

class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;

namespace studio
{

class ModelFileTree;

/// The application window, laid out along the workflow of the bvartools
/// article: a project of models on the left, the five stages -- data, model
/// set-up, posterior simulation, evaluation, application -- as a bar beside
/// it, and the page of the selected stage in the middle.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

    /// Opens a model file, or a folder whose model files form the project.
    void openPath(const QString &path);
    void runAll() { run({Stage::Coefficients, Stage::Forecast, Stage::LogLikelihood}); }
    void showStage(StageId stage);

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
    void showLicences();
    void importData();

public:
    /// Imports a delimited file as the working data set.
    void importFrom(const QString &file);
    /// Adds a model file the builder wrote to the project and selects it.
    void addCreatedModel(const QString &file);
    const DataSet &workingData() const { return data_; }

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
