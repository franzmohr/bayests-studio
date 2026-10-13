// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "app/MainWindow.h"

#include "engine/ModelFileTree.h"
#include "engine/QtReporter.h"
#include "views/BuilderDialog.h"
#include "views/ExtensionsDialog.h"
#include "views/FontDialog.h"
#include "views/PaletteDialog.h"
#include "views/Palettes.h"
#include "views/SignDialog.h"
#include "views/Workbench.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFile>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>

#include <algorithm>

namespace studio
{

namespace
{

/// Names and one-line descriptions of the stages, in StageId order.
struct StageInfo
{
    const char *title;
    const char *detail;
};

constexpr std::array<StageInfo, 5> kStages = {{
    {QT_TRANSLATE_NOOP("Stage", "Data"), QT_TRANSLATE_NOOP("Stage", "The estimation sample")},
    {QT_TRANSLATE_NOOP("Stage", "Model set-up"),
     QT_TRANSLATE_NOOP("Stage", "Specification, priors, initial values")},
    {QT_TRANSLATE_NOOP("Stage", "Posterior simulation"), QT_TRANSLATE_NOOP("Stage", "Run the sampler")},
    {QT_TRANSLATE_NOOP("Stage", "Evaluation"),
     QT_TRANSLATE_NOOP("Stage", "Criteria, forecast performance")},
    {QT_TRANSLATE_NOOP("Stage", "Application"),
     QT_TRANSLATE_NOOP("Stage", "Forecasting, structural analysis")},
}};

} // namespace

MainWindow::MainWindow()
{
    runner_ = new ModelRunner(this);
    tree_ = new ModelFileTree(this);

    buildActions();
    buildLayout();
    setRunning(false);

    connect(runner_, &ModelRunner::stageStarted, this, [this](Stage stage) {
        stageLabel_->setText(stageName(stage));
        progress_->setRange(0, 0);
        log(tr("%1 started.").arg(stageName(stage)));
    });
    connect(runner_, &ModelRunner::progressChanged, this, [this](qint64 done, qint64 total) {
        progress_->setRange(0, static_cast<int>(total));
        progress_->setValue(static_cast<int>(done));
        progress_->setFormat(tr("%v / %m draws"));
    });
    connect(runner_, &ModelRunner::messageEmitted, this, &MainWindow::log);
    connect(runner_, &ModelRunner::runFinished, this,
            [this](bool ok, bool cancelled, const QString &error) {
                (liveTarget_ != nullptr ? liveTarget_ : posterior_)->runFinished();
                liveTarget_ = nullptr;
                setRunning(false);
                if (ok)
                {
                    log(tr("Finished."));
                    statusBar()->showMessage(tr("Run finished"), 5000);
                }
                else if (cancelled)
                {
                    log(tr("Cancelled. Nothing from the interrupted stage was written."));
                    statusBar()->showMessage(tr("Run cancelled"), 5000);
                }
                else
                {
                    log(tr("Failed: %1").arg(error));
                    QMessageBox::warning(this, tr("Run failed"), error);
                }
                refresh();
                emit runEnded(ok);
            });

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    setWindowTitle(QApplication::applicationDisplayName());
    if (!settings.contains(QStringLiteral("window/geometry")))
    {
        // The first start: the workbench wants the whole screen.
        resize(1360, 860);
        // Not offscreen, where the smoke runs size the window themselves.
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
        {
            setWindowState(windowState() | Qt::WindowMaximized);
        }
    }
    // The workbench unless the stages were chosen from the menu. A key of its
    // own: builds before this one stored "window/layout" on every start,
    // which says nothing about what the user picked.
    setWorkbench(settings.value(QStringLiteral("window/arrangement"), QStringLiteral("workbench")).toString() !=
                 QLatin1String("stages"));
    setAssistantEnabled(settings.value(QStringLiteral("assistant/enabled"), false).toBool());
}

void MainWindow::buildActions()
{
    QStyle *style = QApplication::style();
    const auto lastDir = []() { return QSettings().value(QStringLiteral("files/lastDir")).toString(); };
    const auto remember = [](const QString &dir) { QSettings().setValue(QStringLiteral("files/lastDir"), dir); };

    openFolder_ = new QAction(style->standardIcon(QStyle::SP_DirOpenIcon), tr("Open &Project…"), this);
    openFolder_->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
    openFolder_->setToolTip(tr("A folder of model files, compared side by side"));
    connect(openFolder_, &QAction::triggered, this, [this, lastDir, remember]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Project Folder"), lastDir());
        if (!dir.isEmpty())
        {
            remember(dir);
            openPath(dir);
        }
    });

    openFile_ = new QAction(style->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open Model…"), this);
    openFile_->setShortcut(QKeySequence::Open);
    connect(openFile_, &QAction::triggered, this, [this, lastDir, remember]() {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Open Model File"), lastDir(), tr("BayesTS model files (*.h5 *.hdf5);;All files (*)"));
        if (!file.isEmpty())
        {
            remember(QFileInfo(file).absolutePath());
            openPath(file);
        }
    });

    reload_ = new QAction(style->standardIcon(QStyle::SP_BrowserReload), tr("&Reload"), this);
    reload_->setShortcut(QKeySequence::Refresh);
    connect(reload_, &QAction::triggered, this, [this]() {
        if (!projectDir_.isEmpty())
        {
            const QString keep = file_;
            openPath(projectDir_);
            selectModel(keep);
        }
        else
        {
            refresh();
        }
    });

    runAll_ = new QAction(style->standardIcon(QStyle::SP_MediaPlay), tr("Run &All Stages"), this);
    runAll_->setShortcut(Qt::Key_F5);
    runAll_->setToolTip(tr("Posterior simulation, forecast and log likelihood, in that order"));
    connect(runAll_, &QAction::triggered, this, &MainWindow::runAll);
    runCoefficients_ = new QAction(tr("&Posterior Simulation"), this);
    connect(runCoefficients_, &QAction::triggered, this, [this]() { run({Stage::Coefficients}); });
    runForecast_ = new QAction(tr("&Forecast"), this);
    connect(runForecast_, &QAction::triggered, this, [this]() { run({Stage::Forecast}); });
    runLoglik_ = new QAction(tr("&Log Likelihood"), this);
    connect(runLoglik_, &QAction::triggered, this, [this]() { run({Stage::LogLikelihood}); });

    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(openFolder_);
    file->addAction(openFile_);
    file->addAction(reload_);
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *view = menuBar()->addMenu(tr("&Stage"));
    for (int i = 0; i < static_cast<int>(kStages.size()); ++i)
    {
        QAction *a = view->addAction(QStringLiteral("&%1  %2").arg(i + 1).arg(
            QCoreApplication::translate("Stage", kStages[static_cast<std::size_t>(i)].title)));
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        connect(a, &QAction::triggered, this, [this, i]() { showStage(static_cast<StageId>(i)); });
    }

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu *layoutMenu = viewMenu->addMenu(tr("&Layout"));
    auto *layouts = new QActionGroup(this);
    stagesLayout_ = layoutMenu->addAction(tr("&Stages, One at a Time"), this, [this]() { setWorkbench(false, true); });
    workbenchLayout_ = layoutMenu->addAction(tr("&Workbench, Side by Side (Wide Screens)"), this,
                                             [this]() { setWorkbench(true, true); });
    workbenchLayout_->setShortcut(QKeySequence(tr("Ctrl+Shift+W")));
    for (QAction *a : {stagesLayout_, workbenchLayout_})
    {
        a->setCheckable(true);
        layouts->addAction(a);
    }
    stagesLayout_->setChecked(true);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Colour &Palette…"), this, [this]() {
        PaletteDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            setOwnPalettes(dialog.own());
            setCurrentPalette(dialog.chosen());
            // Every view draws again, in the new colours.
            reloadProject();
        }
    });
    viewMenu->addAction(tr("&Font…"), this, [this]() {
        FontDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            setFonts(dialog.chosen());
            // The plots set their axis fonts when they are drawn.
            reloadProject();
        }
    });

    QMenu *runMenu = menuBar()->addMenu(tr("&Run"));
    runMenu->addAction(runAll_);
    runMenu->addSeparator();
    runMenu->addAction(runCoefficients_);
    runMenu->addAction(runForecast_);
    runMenu->addAction(runLoglik_);

    // The assistant's menu, there only while it is turned on.
    assistantMenu_ = menuBar()->addMenu(tr("A&ssistant"));
    for (const Workflow &w : workflows())
    {
        assistantMenu_->addAction(tr("%1…").arg(w.title), this, [this, id = w.id]() {
            ensureAssistant()->start(id);
            assistantDock_->show();
        });
    }
    assistantMenu_->addSeparator();
    assistantMenu_->addAction(tr("Choose a Workflow or an Example…"), this, [this]() {
        ensureAssistant()->showChooser();
        assistantDock_->show();
    });
    viewMenu->addSeparator();
    assistantToggle_ = viewMenu->addAction(tr("&Assistant"), this, [this](bool on) { setAssistantEnabled(on, true); });
    assistantToggle_->setCheckable(true);
    assistantToggle_->setToolTip(tr("Guidance through a workflow, step by step, with worked examples. Optional: "
                                    "everything it does can be done from the stages and the workbench."));

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About %1").arg(QApplication::applicationDisplayName()), this,
                    &MainWindow::showAbout);
    help->addAction(tr("&Licence…"), this, &MainWindow::showLicenceAgreement);
    help->addAction(tr("Third-party &Licences"), this, &MainWindow::showLicences);
    help->addAction(tr("&Extensions…"), this, [this]() {
        ExtensionsDialog dialog(this);
        connect(&dialog, &ExtensionsDialog::reloaded, this, [this]() {
            if (assistant_ != nullptr)
            {
                assistant_->extensionsReloaded();
            }
            // Plots draw again, in a palette an extension may have changed.
            reloadProject();
        });
        dialog.exec();
    });

    QToolBar *bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addAction(openFolder_);
    bar->addAction(openFile_);
    bar->addAction(reload_);
    bar->addSeparator();
    bar->addAction(runAll_);
}

void MainWindow::buildLayout()
{
    // The project: one entry per model file, as Figure 2 of the article runs
    // models A, B and C through the stages side by side.
    models_ = new QListWidget;
    models_->setUniformItemSizes(true);
    models_->setMinimumWidth(210);
    models_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    models_->setToolTip(tr("The model files of the project. Select one to see it at every stage."));
    connect(models_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item != nullptr)
        {
            selectModel(item->data(Qt::UserRole).toString());
        }
    });
    auto *projectDock = new QDockWidget(tr("Project"), this);
    projectDock->setObjectName(QStringLiteral("projectDock"));
    projectDock->setWidget(models_);
    projectDock->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    // The stage bar.
    stages_ = new QListWidget;
    stages_->setObjectName(QStringLiteral("stageBar"));
    stages_->setFixedWidth(300);
    stages_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stages_->setTextElideMode(Qt::ElideNone);
    stages_->setSpacing(2);
    stages_->setFrameShape(QFrame::NoFrame);
    stages_->setToolTip(tr("The stages of a Bayesian VAR analysis, as in the bvartools article: data, set-up, posterior simulation, evaluation and application."));
    for (std::size_t i = 0; i < kStages.size(); ++i)
    {
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(280, 58));
        item->setToolTip(QCoreApplication::translate("Stage", kStages[i].detail));
        stages_->addItem(item);
    }

    pagesStack_ = new QStackedWidget;
    posterior_ = new PosteriorPage(runner_->reporter());
    pages_ = {new DataPage, new SetupPage(tree_), posterior_, new EvaluationPage, new ApplicationPage};
    for (StagePage *page : pages_)
    {
        pagesStack_->addWidget(page);
        connect(page, &StagePage::runRequested, this, &MainWindow::run);
        connect(page, &StagePage::importRequested, this, &MainWindow::importData);
        connect(page, &StagePage::newModelRequested, this, &MainWindow::newModel);
        connect(page, &StagePage::modelCreated, this, &MainWindow::addDerivedModel);
    }
    connect(stages_, &QListWidget::currentRowChanged, pagesStack_, &QStackedWidget::setCurrentIndex);

    auto *central = new QWidget;
    auto *row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(stages_);
    row->addWidget(pagesStack_, 1);
    // The workbench: the same stages side by side, for a wide screen.
    workbench_ = new Workbench(runner_->reporter());
    connect(workbench_, &Workbench::runRequested, this, &MainWindow::run);
    connect(workbench_, &Workbench::importRequested, this, &MainWindow::importData);
    connect(workbench_, &Workbench::modelCreated, this, &MainWindow::addDerivedModel);
    connect(workbench_, &Workbench::modelSpecified, this, [this](const QString &file) {
        addCreatedModel(file);
        runAll();
    });
    layouts_ = new QStackedWidget;
    layouts_->addWidget(central);
    layouts_->addWidget(workbench_);
    setCentralWidget(layouts_);
    updateStageMarks();
    stages_->setCurrentRow(static_cast<int>(StageId::Setup));

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    auto *logDock = new QDockWidget(tr("Log"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setWidget(log_);
    logDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    resizeDocks({logDock}, {120}, Qt::Vertical);

    stageLabel_ = new QLabel;
    progress_ = new QProgressBar;
    progress_->setMaximumWidth(320);
    progress_->setTextVisible(true);
    cancel_ = new QPushButton(tr("Cancel"));
    connect(cancel_, &QPushButton::clicked, runner_, &ModelRunner::cancel);
    cancel_->setToolTip(tr("Stops the run in progress."));
    statusBar()->addPermanentWidget(stageLabel_);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(cancel_);
}

AssistantPanel *MainWindow::ensureAssistant()
{
    if (assistant_ == nullptr)
    {
        // Beside the pages, made the first time it is wanted.
        assistant_ = new AssistantPanel(this);
        assistantDock_ = new QDockWidget(tr("Assistant"), this);
        assistantDock_->setObjectName(QStringLiteral("assistantDock"));
        assistantDock_->setWidget(assistant_);
        assistantDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, assistantDock_);
        resizeDocks({assistantDock_}, {380}, Qt::Horizontal);
        assistant_->projectChanged();
    }
    return assistant_;
}

void MainWindow::setAssistantEnabled(bool on, bool remember)
{
    assistantMenu_->menuAction()->setVisible(on);
    assistantToggle_->setChecked(on);
    if (on)
    {
        ensureAssistant();
        assistantDock_->show();
    }
    else if (assistantDock_ != nullptr)
    {
        assistantDock_->hide();
    }
    if (remember)
    {
        QSettings().setValue(QStringLiteral("assistant/enabled"), on);
    }
}

void MainWindow::setWorkbench(bool on, bool remember)
{
    layouts_->setCurrentWidget(on ? static_cast<QWidget *>(workbench_) : layouts_->widget(0));
    (on ? workbenchLayout_ : stagesLayout_)->setChecked(true);
    if (remember)
    {
        QSettings().setValue(QStringLiteral("window/arrangement"),
                             on ? QStringLiteral("workbench") : QStringLiteral("stages"));
    }
}

bool MainWindow::workbenchActive() const
{
    return layouts_ != nullptr && layouts_->currentWidget() == workbench_;
}

void MainWindow::showStage(StageId stage)
{
    if (workbenchActive())
    {
        setWorkbench(false);
    }
    stages_->setCurrentRow(static_cast<int>(stage));
}

void MainWindow::updateStageMarks()
{
    // A stage is marked done when the model file holds what it produces.
    const bool have = !file_.isEmpty();
    const auto has = [this](const char *path) { return tree_->find(QString::fromLatin1(path)) != nullptr; };
    const std::array<bool, 5> done = {
        have && has("/data/train/y"),
        have && report_.ok,
        have && report_.hasPosterior,
        have && has("/posterior/loglik"),
        have && has("/posterior/forecast/forecasts"),
    };
    for (std::size_t i = 0; i < kStages.size(); ++i)
    {
        QListWidgetItem *item = stages_->item(static_cast<int>(i));
        item->setText(QStringLiteral("%1   %2\n       %3")
                          .arg(done[i] ? QStringLiteral("●") : QStringLiteral("○"))
                          .arg(QCoreApplication::translate("Stage", kStages[i].title),
                               QCoreApplication::translate("Stage", kStages[i].detail)));
    }
}

void MainWindow::openPath(const QString &path)
{
    const QFileInfo info(path);
    projectFiles_.clear();
    if (info.isDir())
    {
        projectDir_ = info.absoluteFilePath();
        // Every model file below the folder, as `bayests` walks a tree.
        QDirIterator it(projectDir_, {QStringLiteral("*.h5"), QStringLiteral("*.hdf5")}, QDir::Files,
                        QDirIterator::Subdirectories);
        // A horse race's windows are the race's, scored on its own tab:
        // hundreds of them would bury the project's models.
        const QString race = QDir(projectDir_).filePath(QStringLiteral("horse-race")) + QLatin1Char('/');
        while (it.hasNext())
        {
            const QString next = it.next();
            if (!next.startsWith(race))
            {
                projectFiles_ << next;
            }
        }
        std::sort(projectFiles_.begin(), projectFiles_.end());
        log(tr("Opened project %1: %2 model files")
                .arg(QDir::toNativeSeparators(projectDir_))
                .arg(projectFiles_.size()));
    }
    else
    {
        projectDir_.clear();
        projectFiles_ << info.absoluteFilePath();
        log(tr("Opened %1").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    }

    {
        const QSignalBlocker block(models_);
        models_->clear();
        const QDir base(projectDir_.isEmpty() ? info.absolutePath() : projectDir_);
        for (const QString &file : projectFiles_)
        {
            auto *item = new QListWidgetItem(QApplication::style()->standardIcon(QStyle::SP_FileIcon),
                                             base.relativeFilePath(file));
            item->setData(Qt::UserRole, file);
            item->setToolTip(QDir::toNativeSeparators(file));
            models_->addItem(item);
        }
    }
    file_.clear();
    if (!projectFiles_.isEmpty())
    {
        selectModel(projectFiles_.front());
    }
    else
    {
        refresh();
    }
}

void MainWindow::selectModel(const QString &file)
{
    {
        const QSignalBlocker block(models_);
        for (int i = 0; i < models_->count(); ++i)
        {
            if (models_->item(i)->data(Qt::UserRole).toString() == file)
            {
                models_->setCurrentRow(i);
            }
        }
    }
    file_ = file;
    refresh();
}

void MainWindow::refresh()
{
    report_ = {};
    if (!file_.isEmpty())
    {
        try
        {
            tree_->load(file_);
            report_ = ModelRunner::check(file_, QString());
        }
        catch (const std::exception &e)
        {
            tree_->clear();
            QMessageBox::warning(this, tr("Cannot open file"),
                                 tr("%1 could not be read:\n%2").arg(file_, QString::fromUtf8(e.what())));
            file_.clear();
        }
    }
    else
    {
        tree_->clear();
    }

    const ModelContext context{file_, tree_, report_, projectFiles_, projectDir_};
    for (StagePage *page : pages_)
    {
        page->showModel(context);
    }
    workbench_->setProjectFolder(projectDir_);
    workbench_->showModel(context);
    updateStageMarks();
    if (assistant_ != nullptr)
    {
        assistant_->projectChanged();
    }

    setWindowTitle(file_.isEmpty()
                       ? QApplication::applicationDisplayName()
                       : QStringLiteral("%1 — %2").arg(QFileInfo(file_).fileName(),
                                                        QApplication::applicationDisplayName()));
    setRunning(false);
}

void MainWindow::run(const std::vector<Stage> &stages)
{
    if (file_.isEmpty() || runner_->isRunning())
    {
        return;
    }
    setRunning(true);
    if (std::find(stages.begin(), stages.end(), Stage::Coefficients) != stages.end())
    {
        if (workbenchActive())
        {
            liveTarget_ = workbench_->posterior();
        }
        else
        {
            showStage(StageId::Posterior);
            liveTarget_ = posterior_;
        }
        liveTarget_->runStarted();
    }
    runner_->start(file_, QString(), stages);
}

void MainWindow::setRunning(bool running)
{
    const bool ok = !file_.isEmpty() && report_.ok;
    openFile_->setEnabled(!running);
    openFolder_->setEnabled(!running);
    reload_->setEnabled(!running && !file_.isEmpty());
    models_->setEnabled(!running);
    runAll_->setEnabled(!running && ok);
    runCoefficients_->setEnabled(!running && ok);
    runForecast_->setEnabled(!running && ok && report_.hasPosterior);
    runLoglik_->setEnabled(!running && ok && report_.hasPosterior);
    cancel_->setVisible(running);
    progress_->setVisible(running);
    stageLabel_->setVisible(running);
    if (!running)
    {
        progress_->reset();
    }
}

void MainWindow::log(const QString &text)
{
    log_->appendPlainText(QStringLiteral("%1  %2").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (runner_->isRunning())
    {
        const auto answer = QMessageBox::question(
            this, tr("A run is in progress"),
            tr("Cancel the run and quit? Draws of the interrupted stage are not written."));
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        runner_->cancel();
        while (runner_->isRunning())
        {
            QApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    event->accept();
}

void MainWindow::importData()
{
    QSettings settings;
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import Data"), settings.value(QStringLiteral("files/lastDir")).toString(),
        tr("Delimited text (*.csv *.tsv *.txt);;All files (*)"));
    if (!file.isEmpty())
    {
        settings.setValue(QStringLiteral("files/lastDir"), QFileInfo(file).absolutePath());
        importFrom(file);
    }
}

void MainWindow::importFrom(const QString &file)
{
    try
    {
        data_ = readDelimited(file);
    }
    catch (const std::exception &e)
    {
        QMessageBox::warning(this, tr("Cannot import"), tr("%1:\n%2").arg(file, QString::fromUtf8(e.what())));
        return;
    }
    log(tr("Imported %1: %2 series, %3 periods")
            .arg(QDir::toNativeSeparators(file))
            .arg(data_.series())
            .arg(data_.length()));
    static_cast<DataPage *>(pages_[static_cast<std::size_t>(StageId::Data)])->showDataSet(data_);
    static_cast<SetupPage *>(pages_[static_cast<std::size_t>(StageId::Setup)])->setDataAvailable(true);
    workbench_->setProjectFolder(projectDir_);
    workbench_->setData(data_);
    if (!workbenchActive())
    {
        showStage(StageId::Data);
    }
    if (assistant_ != nullptr)
    {
        assistant_->projectChanged();
    }
}

bool MainWindow::isRunning() const
{
    return runner_->isRunning();
}

void MainWindow::showStageTab(WorkflowStage stage, const QString &tab)
{
    const auto id = static_cast<StageId>(static_cast<int>(stage));
    showStage(id);
    if (tab.isEmpty())
    {
        return;
    }
    // The tab by its title, in whichever tab widget of the page holds it.
    for (QTabWidget *tabs : pages_[static_cast<std::size_t>(id)]->findChildren<QTabWidget *>())
    {
        for (int i = 0; i < tabs->count(); ++i)
        {
            if (tabs->tabText(i).remove(QLatin1Char('&')) == tab)
            {
                tabs->setCurrentIndex(i);
            }
        }
    }
}

void MainWindow::openSignRestrictions(const std::vector<SignRestriction> &preset, int rotated)
{
    if (file_.isEmpty())
    {
        return;
    }
    const StructuralSupport support = structuralSupport(file_);
    if (!support.ok || !support.signAllowed)
    {
        QMessageBox::information(this, tr("Sign Restrictions"),
                                 support.ok ? support.signRefusal : support.reason);
        return;
    }
    SignDialog dialog(file_, support, this);
    dialog.preset(preset, rotated);
    if (dialog.exec() == QDialog::Accepted)
    {
        if (!dialog.createdFile().isEmpty())
        {
            addDerivedModel(dialog.createdFile());
            return;
        }
        refresh();
    }
}

void MainWindow::reloadProject()
{
    if (projectDir_.isEmpty())
    {
        refresh();
        return;
    }
    const QString keep = file_;
    openPath(projectDir_);
    if (!keep.isEmpty())
    {
        selectModel(keep);
    }
}

void MainWindow::newModel()
{
    if (data_.empty())
    {
        return;
    }
    BuilderDialog dialog(data_, projectDir_, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    addCreatedModel(dialog.createdFile());
}

void MainWindow::addCreatedModel(const QString &created)
{
    log(tr("Created %1").arg(QDir::toNativeSeparators(created)));
    // The new model joins the project -- the folder it was saved in, when
    // there was no project or it was saved outside it.
    const QFileInfo info(created);
    if (projectDir_.isEmpty() || !info.absoluteFilePath().startsWith(projectDir_))
    {
        openPath(info.absolutePath());
    }
    else
    {
        openPath(projectDir_);
    }
    selectModel(info.absoluteFilePath());
    if (!workbenchActive())
    {
        showStage(StageId::Posterior);
    }
}

void MainWindow::addDerivedModel(const QString &file)
{
    addCreatedModel(file);
    if (workbenchActive())
    {
        return;   // the impulse responses are on screen already
    }
    showStage(StageId::Application);
    static_cast<ApplicationPage *>(pages_[static_cast<std::size_t>(StageId::Application)])->showStructural();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About %1").arg(QApplication::applicationDisplayName()),
        tr("<h3>%1 %2</h3>"
           "<p>Specify, estimate and analyse Bayesian time series models.</p>"
           "<p>Estimation by the BayesTS library.</p>")
            .arg(QApplication::applicationDisplayName(), QApplication::applicationVersion()));
}

void MainWindow::showLicenceAgreement()
{
    // The studio's own licence, BSD-3-Clause, as it shipped.
    QFile file(QStringLiteral(":/licence/LICENSE"));
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Licence"));
    dialog.resize(680, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(QString::fromUtf8(file.readAll()));
    text->setObjectName(QStringLiteral("licenceText"));
    text->setReadOnly(true);
    text->setToolTip(tr("%1 is free and open-source software under this licence; the licences of the "
                        "components it ships are under Help > Third-party Licences.")
                         .arg(QApplication::applicationDisplayName()));
    layout->addWidget(text);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(box);
    dialog.exec();
}

void MainWindow::showLicences()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Third-party Licences"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<p>This application uses the following software:</p><ul>"
        "<li><b>BayesTS</b> — BSD 3-Clause</li>"
        "<li><b>Qt</b> — GNU LGPL v3; linked dynamically. Qt sources: "
        "<a href='https://download.qt.io/'>download.qt.io</a></li>"
        "<li><b>Qwt</b> — Qwt License 1.0 (LGPL 2.1 with exceptions)</li>"
        "<li><b>Armadillo</b> — Apache 2.0</li>"
        "<li><b>OpenBLAS</b> — BSD 3-Clause</li>"
        "<li><b>HDF5</b> — BSD-style (The HDF Group)</li>"
        "<li><b>HighFive</b> — Boost Software License 1.0</li>"
        "<li><b>Inter</b> — Copyright (c) 2016 The Inter Project Authors; SIL Open Font License 1.1</li>"
        "</ul><p>The colour palettes:</p><ul>"
        "<li><b>ggplot2's hue palette</b> — from scales, Copyright (c) 2025 scales authors; MIT</li>"
        "<li><b>Set1, Set2, Dark2, Paired</b> — Colors from www.ColorBrewer.org by Cynthia A. Brewer, "
        "Geography, Pennsylvania State University; Apache License 2.0</li>"
        "<li><b>tab10, tab20, tab20b, tab20c, petroff10</b> — from matplotlib, Copyright (c) 2012- "
        "Matplotlib Development Team; All Rights Reserved; matplotlib licence</li>"
        "<li><b>viridis, magma, inferno, plasma, cividis</b> — from viridisLite, Copyright (c) 2015 Simon Garnier; "
        "MIT. The maps by Nathaniel Smith, Stéfan van der Walt and Eric Firing (CC0) and, cividis, Jamie R. "
        "Nuñez, Christopher R. Anderton and Ryan S. Renslow</li>"
        "</ul><p>Full licence texts are installed in the <i>licenses</i> folder.</p>"));
    box.exec();
}

} // namespace studio
