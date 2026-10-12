#pragma once

#include <QWidget>

#include <vector>

#include "engine/DataSet.h"
#include "engine/Examples.h"
#include "engine/Structural.h"
#include "engine/Workflow.h"

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QVBoxLayout;

namespace studio
{

class BatchRunner;

/// What the assistant asks of the application window.
class AssistantHost
{
public:
    virtual ~AssistantHost() = default;
    virtual QWidget *hostWindow() = 0;
    virtual QString projectFolder() const = 0;
    virtual QStringList projectModels() const = 0;
    virtual QString currentModel() const = 0;
    virtual const DataSet &workingData() const = 0;
    virtual void openProject(const QString &folder) = 0;
    virtual void importDataFile(const QString &file) = 0;
    virtual void chooseDataFile() = 0;
    virtual void openBuilder() = 0;
    virtual void addModel(const QString &file) = 0;
    virtual void selectModelFile(const QString &file) = 0;
    virtual void runAllStages() = 0;
    virtual bool isRunning() const = 0;
    virtual void showStageTab(WorkflowStage stage, const QString &tab) = 0;
    virtual void openSignRestrictions(const std::vector<SignRestriction> &preset) = 0;
    virtual void reloadProject() = 0;
};

/// The assistant: a workflow -- a master thesis, a forecast horse race -- as
/// steps, each with what to do and why, the actions that do it, and the stage
/// it concerns, which the assistant opens. A step ticks itself off when the
/// project shows it done, or when the user confirms a judgement only they can
/// make, and the assistant moves on. A worked example -- a published paper --
/// fills in the data, the specification and the identification, and says at
/// every step what the paper does there and what to expect.
class AssistantPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssistantPanel(AssistantHost *host, QWidget *parent = nullptr);

    /// The project changed: a folder opened, a model run, a file written.
    void projectChanged();
    /// Starts `workflow` from scratch, or with `example`.
    void start(const QString &workflow, const QString &example = {});
    void showChooser();
    /// After Help > Extensions > Reload: the chooser and the example again.
    void extensionsReloaded();

private:
    void rebuildChooser();
    ProjectState state() const;
    void saveRecord();
    void refreshSteps(bool follow);
    void showStep(int index);
    void act(const QString &id);
    bool ensureProject();
    bool ensureData();
    QString reproductionsFolder();
    std::optional<VarBuilderSpec> baselineSpec() const;
    void createAndRun(const std::vector<std::pair<QString, VarBuilderSpec>> &variants, const QString &event);
    void runFiles(const QStringList &files, const QString &event);
    void exportThesis();
    void designRace();
    void runRace();
    void exportRace();

    AssistantHost *host_;
    const Workflow *workflow_ = nullptr;
    const WorkflowExample *example_ = nullptr;
    AssistantRecord record_;
    QString recordFolder_;
    int shown_ = -1;
    int current_ = -1;

    QStackedWidget *pages_;
    QVBoxLayout *chooser_;
    QLabel *title_;
    QLabel *exampleLabel_;
    QListWidget *steps_;
    QTextBrowser *guidance_;
    QCheckBox *confirm_;
    QHBoxLayout *actions_;
    QWidget *actionsHost_;
    QProgressBar *progress_;
    QLabel *status_;
    QPushButton *back_;
    QPushButton *next_;
    BatchRunner *batch_;
    mutable ProjectState cached_;   ///< the files as last read, while a run holds them
};

} // namespace studio
