#include "views/AssistantPanel.h"
#include "views/Fonts.h"

#include "engine/BatchRunner.h"
#include "engine/HorseRace.h"
#include "engine/ModelFileTree.h"
#include "engine/TableFormat.h"
#include "views/AssistantDialogs.h"
#include "views/FigureExport.h"
#include "views/HorseRaceView.h"
#include "views/TableExport.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <highfive/highfive.hpp>

#include <qwt_plot.h>
#include <qwt_plot_item.h>

namespace studio
{

namespace
{

QLabel *subtle(const QString &text = {})
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("subtle"));
    l->setWordWrap(true);
    return l;
}

bool hasForecast(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        return h5.exist("/posterior/forecast/forecasts");
    }
    catch (const std::exception &)
    {
        return false;
    }
}

/// Whether a figure shows anything: a view without a model still has its
/// empty plots.
bool hasContent(const FigureSpec &spec)
{
    for (const FigureCell &c : spec.cells)
    {
        if (c.plot == nullptr)
        {
            continue;
        }
        for (const QwtPlotItem *item : c.plot->itemList())
        {
            const int rtti = item->rtti();
            if (rtti == QwtPlotItem::Rtti_PlotCurve || rtti == QwtPlotItem::Rtti_PlotIntervalCurve ||
                rtti == QwtPlotItem::Rtti_PlotHistogram || rtti == QwtPlotItem::Rtti_PlotMultiBarChart ||
                rtti == QwtPlotItem::Rtti_PlotBarChart || rtti == QwtPlotItem::Rtti_PlotTradingCurve)
            {
                return true;
            }
        }
    }
    return false;
}

QString stem(const QString &text)
{
    QString s;
    for (const QChar c : text.toLower())
    {
        if (c.isLetterOrNumber() && c.unicode() < 128)
        {
            s += c;
        }
        else if (!s.isEmpty() && !s.endsWith(QLatin1Char('-')))
        {
            s += QLatin1Char('-');
        }
    }
    while (s.endsWith(QLatin1Char('-')))
    {
        s.chop(1);
    }
    return s.isEmpty() ? QStringLiteral("item") : s.left(50);
}

} // namespace

AssistantPanel::AssistantPanel(AssistantHost *host, QWidget *parent) : QWidget(parent), host_(host)
{
    batch_ = new BatchRunner(this);
    setMinimumWidth(340);

    // The chooser.
    auto *chooserHost = new QWidget;
    chooser_ = new QVBoxLayout(chooserHost);
    rebuildChooser();
    auto *chooserScroll = new QScrollArea;
    chooserScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chooserScroll->setWidget(chooserHost);
    chooserScroll->setWidgetResizable(true);
    chooserScroll->setFrameShape(QFrame::NoFrame);

    // A workflow under way.
    auto *flow = new QWidget;
    auto *fl = new QVBoxLayout(flow);
    fl->setContentsMargins(0, 0, 0, 0);
    title_ = new QLabel;
    title_->setObjectName(QStringLiteral("sectionTitle"));
    title_->setWordWrap(true);
    exampleLabel_ = subtle();
    auto *change = new QPushButton(tr("Change…"));
    connect(change, &QPushButton::clicked, this, &AssistantPanel::showChooser);
    change->setToolTip(tr("Back to the list of workflows and worked examples. What was done so far is kept in the project."));
    auto *head = new QHBoxLayout;
    auto *headText = new QVBoxLayout;
    headText->addWidget(title_);
    headText->addWidget(exampleLabel_);
    head->addLayout(headText, 1);
    head->addWidget(change, 0, Qt::AlignTop);
    fl->addLayout(head);
    steps_ = new QListWidget;
    steps_->setObjectName(QStringLiteral("assistantSteps"));
    steps_->setMaximumHeight(240);
    // Long step titles wrap rather than scroll sideways.
    steps_->setWordWrap(true);
    steps_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    steps_->setResizeMode(QListView::Adjust);
    steps_->setToolTip(tr("The steps of the workflow: a tick when done, a dot at the current one. Click one to read it and open the stage it concerns."));
    connect(steps_, &QListWidget::currentRowChanged, this, [this](int row) {
        showStep(row);
        if (workflow_ && row >= 0 && row < static_cast<int>(workflow_->steps.size()))
        {
            const WorkflowStep &s = workflow_->steps[static_cast<std::size_t>(row)];
            host_->showStageTab(s.stage, s.tab);
        }
    });
    fl->addWidget(steps_);
    guidance_ = new QTextBrowser;
    // Read at length: half a point larger, with room between the lines.
    guidance_->setFont(readingFont());
    guidance_->document()->setDefaultStyleSheet(QStringLiteral("p, li { line-height: 125%; }"));
    guidance_->setOpenExternalLinks(true);
    guidance_->setObjectName(QStringLiteral("assistantGuidance"));
    fl->addWidget(guidance_, 1);
    confirm_ = new QCheckBox;
    confirm_->setObjectName(QStringLiteral("assistantConfirm"));
    confirm_->setToolTip(tr("A judgement only you can make: tick it when it is made, and the assistant moves on."));
    connect(confirm_, &QCheckBox::toggled, this, [this](bool on) {
        if (!workflow_ || shown_ < 0)
        {
            return;
        }
        const QString id = workflow_->steps[static_cast<std::size_t>(shown_)].id;
        if (on)
        {
            record_.confirmed << id;
        }
        else
        {
            record_.confirmed.remove(id);
        }
        saveRecord();
        refreshSteps(true);
    });
    fl->addWidget(confirm_);
    actionsHost_ = new QWidget;
    actions_ = new QHBoxLayout(actionsHost_);
    actions_->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(actionsHost_);
    progress_ = new QProgressBar;
    progress_->setVisible(false);
    status_ = subtle();
    fl->addWidget(progress_);
    fl->addWidget(status_);
    back_ = new QPushButton(tr("‹ Back"));
    next_ = new QPushButton(tr("Next ›"));
    next_->setObjectName(QStringLiteral("assistantNext"));
    next_->setToolTip(tr("The next step. Steps can be read ahead of time; they tick themselves off as the project shows them done."));
    back_->setToolTip(tr("The previous step."));
    connect(back_, &QPushButton::clicked, this, [this]() { steps_->setCurrentRow(std::max(0, shown_ - 1)); });
    connect(next_, &QPushButton::clicked, this, [this]() {
        if (workflow_)
        {
            steps_->setCurrentRow(std::min(static_cast<int>(workflow_->steps.size()) - 1, shown_ + 1));
        }
    });
    auto *nav = new QHBoxLayout;
    nav->addWidget(back_);
    nav->addStretch(1);
    nav->addWidget(next_);
    fl->addLayout(nav);

    pages_ = new QStackedWidget;
    pages_->addWidget(chooserScroll);
    pages_->addWidget(flow);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(pages_);

    connect(batch_, &BatchRunner::progress, this, [this](int done, int total, const QString &file) {
        progress_->setRange(0, total);
        progress_->setValue(done);
        status_->setText(tr("Estimated %1 of %2: %3").arg(done).arg(total).arg(QFileInfo(file).completeBaseName()));
    });
    connect(batch_, &BatchRunner::failed, this, [this](const QString &file, const QString &message) {
        status_->setText(tr("✗  %1: %2").arg(QFileInfo(file).fileName(), message));
    });
    connect(batch_, &BatchRunner::finished, this, [this](int ok, int failed, bool cancelled) {
        progress_->setVisible(false);
        const QString event = property("pendingEvent").toString();
        if (ok > 0 && !event.isEmpty())
        {
            record_.events << event;
            saveRecord();
        }
        status_->setText(cancelled ? tr("Stopped; what was estimated is kept, and running again resumes.")
                         : failed > 0 ? tr("%1 estimated, %2 failed.").arg(ok).arg(failed)
                                      : tr("All %1 estimated.").arg(ok));
        host_->reloadProject();
        if (auto *race = host_->hostWindow()->findChild<HorseRaceView *>())
        {
            race->reload();
        }
        refreshSteps(true);
    });
    showChooser();
}

void AssistantPanel::rebuildChooser()
{
    while (QLayoutItem *item = chooser_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    chooser_->addWidget(subtle(tr("The assistant guides you through a workflow step by step: what to do, why, the "
                                  "usual choices and their references, and the buttons that do it. Start from your "
                                  "own data, or follow a published paper.")));
    // The examples of a workflow that `papers` selects -- the papers it
    // reproduces or its own -- as buttons, then those not yet guided. Returns
    // whether there were any.
    const auto addExamples = [this](QVBoxLayout *bl, const Workflow &w, bool papers) {
        QStringList later;
        bool any = false;
        for (const WorkflowExample *e : examplesFor(w.id))
        {
            if (e->reproduction != papers)
            {
                continue;
            }
            any = true;
            if (!e->supported)
            {
                later << QStringLiteral("<li>%1 -- <i>%2</i></li>").arg(e->title.toHtmlEscaped(), e->missing.toHtmlEscaped());
                continue;
            }
            // "Topic: Authors (year)": the topic as a line, the authors on the button.
            const QString topic = e->title.section(QStringLiteral(": "), 0, 0);
            const QString who = e->title.section(QStringLiteral(": "), 1);
            bl->addWidget(subtle(tr("Worked example -- %1").arg(topic)));
            auto *b = new QPushButton(who.isEmpty() ? e->title : who);
            b->setObjectName(QStringLiteral("example-%1").arg(e->id));
            b->setToolTip(e->question);
            connect(b, &QPushButton::clicked, this, [this, wid = w.id, eid = e->id]() { start(wid, eid); });
            bl->addWidget(b);
        }
        if (!later.isEmpty())
        {
            auto *note = subtle(tr("Not yet guided:<ul>%1</ul>").arg(later.join(QString())));
            note->setTextFormat(Qt::RichText);
            bl->addWidget(note);
        }
        return any;
    };
    // Each workflow with its own examples, then every paper reproduced, by
    // the workflow it follows.
    for (const Workflow &w : workflows())
    {
        auto *box = new QGroupBox(w.title);
        auto *bl = new QVBoxLayout(box);
        auto *audience = new QLabel(QStringLiteral("<b>%1</b>").arg(w.audience));
        bl->addWidget(audience);
        bl->addWidget(subtle(w.summary));
        auto *scratch = new QPushButton(tr("Start with My Own Data"));
        scratch->setObjectName(QStringLiteral("start-%1").arg(w.id));
        scratch->setToolTip(tr("Follow this workflow on a data set of your own, from importing it to the write-up."));
        connect(scratch, &QPushButton::clicked, this, [this, id = w.id]() { start(id); });
        bl->addWidget(scratch);
        addExamples(bl, w, false);
        chooser_->addWidget(box);
    }
    auto *papers = new QGroupBox(tr("Reproducing papers"));
    papers->setObjectName(QStringLiteral("reproducingPapers"));
    auto *pl = new QVBoxLayout(papers);
    pl->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("For following a published study"))));
    pl->addWidget(subtle(tr("Published studies, each implemented with the steps of one of the workflows above: the "
                            "paper's data and specification filled in, and what the paper finds to compare "
                            "against.")));
    for (const Workflow &w : workflows())
    {
        auto *group = new QWidget;
        auto *gl = new QVBoxLayout(group);
        gl->setContentsMargins(0, 6, 0, 0);
        // Named for what the papers do; the introductory workflow's title
        // would call them introductions.
        const QString heading = w.id == QLatin1String("thesis")
                                    ? tr("Structural analysis, with the steps of the introductory examples")
                                    : w.title;
        auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(heading.toHtmlEscaped()));
        label->setWordWrap(true);
        gl->addWidget(label);
        if (addExamples(gl, w, true))
        {
            pl->addWidget(group);
        }
        else
        {
            delete group;
        }
    }
    chooser_->addWidget(papers);
    chooser_->addStretch(1);
}

void AssistantPanel::extensionsReloaded()
{
    // The examples were read again: every pointer to one is stale.
    example_ = findExample(record_.example);
    rebuildChooser();
    if (workflow_)
    {
        refreshSteps(false);
    }
}

void AssistantPanel::showChooser()
{
    pages_->setCurrentIndex(0);
}

void AssistantPanel::start(const QString &workflow, const QString &example)
{
    const Workflow *w = findWorkflow(workflow);
    if (!w || !ensureProject())
    {
        return;
    }
    workflow_ = w;
    example_ = example.isEmpty() ? nullptr : findExample(example);
    record_ = readAssistantRecord(host_->projectFolder());
    recordFolder_ = host_->projectFolder();
    if (record_.workflow != workflow || record_.example != example)
    {
        record_ = AssistantRecord{};
    }
    record_.workflow = workflow;
    record_.example = example;
    saveRecord();
    pages_->setCurrentIndex(1);
    current_ = -1;
    refreshSteps(true);
}

void AssistantPanel::projectChanged()
{
    const QString folder = host_->projectFolder();
    if (!folder.isEmpty() && folder != recordFolder_)
    {
        recordFolder_ = folder;
        record_ = readAssistantRecord(folder);
        workflow_ = findWorkflow(record_.workflow);
        example_ = findExample(record_.example);
        if (workflow_)
        {
            pages_->setCurrentIndex(1);
            current_ = -1;
            // The data the assistant imported, again.
            const QString data = record_.dataFile.isEmpty() ? QString() : QDir(folder).filePath(record_.dataFile);
            if (host_->workingData().empty() && !data.isEmpty() && QFile::exists(data))
            {
                host_->importDataFile(data);
            }
        }
    }
    if (workflow_)
    {
        refreshSteps(true);
    }
}

ProjectState AssistantPanel::state() const
{
    if (host_->isRunning() || batch_->isRunning())
    {
        // A model is being estimated, in this process or by a batch of
        // bayests processes: its file is the sampler's until the run ends,
        // and the project is read again then.
        ProjectState st = cached_;
        st.hasData = !host_->workingData().empty();
        st.confirmed = record_.confirmed;
        st.events = record_.events;
        st.baselineModels = record_.models;
        return st;
    }
    ProjectState st;
    st.folder = host_->projectFolder();
    st.hasData = !host_->workingData().empty();
    st.currentModel = host_->currentModel();
    for (const QString &f : host_->projectModels())
    {
        st.models.push_back(modelFacts(f));
    }
    st.confirmed = record_.confirmed;
    st.events = record_.events;
    st.baselineModels = record_.models;
    const QString race = QDir(st.folder).filePath(QStringLiteral("horse-race"));
    if (!st.folder.isEmpty() && QFile::exists(QDir(race).filePath(QStringLiteral("horse-race.json"))))
    {
        try
        {
            const HorseRaceManifest m = readHorseRace(race);
            st.horseRace = true;
            st.horseRaceModels = m.models();
            for (const auto &c : m.files)
            {
                for (const QString &f : c)
                {
                    st.horseRaceRun += hasForecast(f) ? 1 : 0;
                }
            }
        }
        catch (const std::exception &)
        {
        }
    }
    cached_ = st;
    return st;
}

void AssistantPanel::saveRecord()
{
    if (!host_->projectFolder().isEmpty())
    {
        writeAssistantRecord(host_->projectFolder(), record_);
    }
}

void AssistantPanel::refreshSteps(bool follow)
{
    if (!workflow_)
    {
        return;
    }
    // A paper reproduced is under its own heading, as in the chooser.
    title_->setText(example_ && example_->reproduction ? tr("Reproducing papers") : workflow_->title);
    exampleLabel_->setText(example_ ? tr("Following %1").arg(example_->reference) : workflow_->audience);
    const ProjectState st = state();
    const int current = currentStep(*workflow_, st);
    {
        const QSignalBlocker block(steps_);
        const int keep = steps_->currentRow();
        steps_->clear();
        for (std::size_t i = 0; i < workflow_->steps.size(); ++i)
        {
            const WorkflowStep &s = workflow_->steps[i];
            const bool done = stepDone(s, st);
            const QString mark = done ? QStringLiteral("✓") : (static_cast<int>(i) == current ? QStringLiteral("●")
                                                                                              : QStringLiteral("○"));
            auto *item = new QListWidgetItem(QStringLiteral("%1  %2. %3%4")
                                                 .arg(mark)
                                                 .arg(i + 1)
                                                 .arg(s.title)
                                                 .arg(s.optional ? tr("  (optional)") : QString()));
            item->setToolTip(s.summary);
            if (done)
            {
                item->setForeground(QColor(30, 123, 74));
            }
            if (static_cast<int>(i) == current && !done)
            {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
            steps_->addItem(item);
        }
        steps_->setCurrentRow(keep >= 0 ? keep : current);
    }
    // Moving on: when the step being worked on is done, the next one opens,
    // and the stage it concerns with it.
    if (follow && current != current_)
    {
        current_ = current;
        steps_->setCurrentRow(current);
        const WorkflowStep &s = workflow_->steps[static_cast<std::size_t>(current)];
        host_->showStageTab(s.stage, s.tab);
    }
    showStep(steps_->currentRow());
}

void AssistantPanel::showStep(int index)
{
    if (!workflow_ || index < 0 || index >= static_cast<int>(workflow_->steps.size()))
    {
        return;
    }
    shown_ = index;
    const WorkflowStep &s = workflow_->steps[static_cast<std::size_t>(index)];
    const ProjectState st = state();
    QString html = QStringLiteral("<h3>%1. %2</h3><p><i>%3</i></p>%4")
                       .arg(index + 1)
                       .arg(s.title.toHtmlEscaped(), s.summary.toHtmlEscaped(), s.guidance);
    if (example_)
    {
        const auto note = example_->notes.find(s.id);
        if (note != example_->notes.end())
        {
            html += QStringLiteral("<table width='100%' cellpadding='8' style='background-color:#e9f1fa;'><tr><td>"
                                   "<b>%1</b><br/>%2</td></tr></table>")
                        .arg(tr("In %1").arg(example_->title).toHtmlEscaped(), note->second.toHtmlEscaped());
        }
        const bool last = index == static_cast<int>(workflow_->steps.size()) - 1;
        if ((s.id == QLatin1String("responses") || s.id == QLatin1String("stability") || last) &&
            !example_->findings.isEmpty())
        {
            html += QStringLiteral("<p><b>%1</b> %2</p>").arg(tr("What the paper and its reproduction find:"),
                                                               example_->findings.toHtmlEscaped());
        }
    }
    if (stepDone(s, st))
    {
        html += QStringLiteral("<p style='color:#1E7B4A;'><b>%1</b></p>").arg(tr("✓  Done."));
    }
    guidance_->setHtml(html);

    {
        const QSignalBlocker block(confirm_);
        confirm_->setVisible(!s.confirm.isEmpty());
        confirm_->setText(s.confirm);
        confirm_->setChecked(record_.confirmed.contains(s.id));
    }
    while (QLayoutItem *item = actions_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    std::vector<WorkflowAction> actions = s.actions;
    if (example_ && s.id == QLatin1String("identify") && !example_->restrictions.empty())
    {
        actions.insert(actions.begin(), {QStringLiteral("exampleSigns"), tr("Fill In the Paper's Restrictions…"),
                                         tr("Opens Sign Restrictions with the grid of the paper")});
    }
    for (const WorkflowAction &a : actions)
    {
        QString label = a.label;
        if (example_ && !example_->recipe.file.isEmpty() && a.id == QLatin1String("import"))
        {
            label = isBuiltinData(example_->recipe) ? tr("Prepare the Example's Data") : tr("Prepare the Paper's Data");
        }
        auto *b = new QPushButton(label);
        b->setToolTip(a.tip);
        b->setObjectName(QStringLiteral("action-%1").arg(a.id));
        connect(b, &QPushButton::clicked, this, [this, id = a.id]() { act(id); });
        actions_->addWidget(b);
    }
    actions_->addStretch(1);
    back_->setEnabled(index > 0);
    next_->setEnabled(index + 1 < static_cast<int>(workflow_->steps.size()));
}

bool AssistantPanel::ensureProject()
{
    if (!host_->projectFolder().isEmpty())
    {
        return true;
    }
    QSettings settings;
    const QString dir = QFileDialog::getExistingDirectory(
        host_->hostWindow(), tr("Choose a Folder for the Project -- an empty one for a new project"),
        settings.value(QStringLiteral("files/lastDir")).toString());
    if (dir.isEmpty())
    {
        return false;
    }
    settings.setValue(QStringLiteral("files/lastDir"), dir);
    host_->openProject(dir);
    return !host_->projectFolder().isEmpty();
}

QString AssistantPanel::reproductionsFolder()
{
    QSettings settings;
    // An extension's data lie beside its file.
    if (example_ && !example_->dataRoot.isEmpty())
    {
        return example_->dataRoot;
    }
    QString root = settings.value(QStringLiteral("assistant/reproductions"), defaultReproductionsFolder()).toString();
    if (example_ && !QDir(QDir(root).filePath(example_->folder)).exists())
    {
        root = QFileDialog::getExistingDirectory(host_->hostWindow(),
                                                 tr("Locate the Reproduction Projects (the folder holding \"%1\")")
                                                     .arg(example_->folder),
                                                 root);
        if (!root.isEmpty())
        {
            settings.setValue(QStringLiteral("assistant/reproductions"), root);
        }
    }
    return root;
}

bool AssistantPanel::ensureData()
{
    if (!host_->workingData().empty())
    {
        return true;
    }
    QMessageBox::information(host_->hostWindow(), tr("Assistant"), tr("Import the data first: step 1."));
    return false;
}

std::optional<VarBuilderSpec> AssistantPanel::baselineSpec() const
{
    return specFromJson(record_.baseline, host_->workingData());
}

void AssistantPanel::act(const QString &id)
{
    const QString folder = host_->projectFolder();
    if (id == QLatin1String("import"))
    {
        if (!ensureProject())
        {
            return;
        }
        if (example_ && example_->supported && !example_->recipe.file.isEmpty())
        {
            try
            {
                // The studio's own data need no reproductions folder.
                const QString root = isBuiltinData(example_->recipe) ? QString() : reproductionsFolder();
                const DataSet data = prepareExampleData(example_->recipe, root);
                QDir(host_->projectFolder()).mkpath(QStringLiteral("data"));
                const QString file = QDir(host_->projectFolder()).filePath(QStringLiteral("data/%1.csv").arg(example_->id));
                writeDataSet(data, file);
                record_.dataFile = QDir(host_->projectFolder()).relativeFilePath(file);
                saveRecord();
                host_->importDataFile(file);
                status_->setText((isBuiltinData(example_->recipe)
                                      ? tr("Prepared %1 series over %2 periods from the data the studio ships into %3.")
                                      : tr("Prepared %1 series over %2 periods from the paper's files into %3."))
                                     .arg(data.series())
                                     .arg(data.length())
                                     .arg(record_.dataFile));
            }
            catch (const std::exception &e)
            {
                QMessageBox::warning(host_->hostWindow(), tr("Prepare the Data"), QString::fromUtf8(e.what()));
            }
        }
        else
        {
            host_->chooseDataFile();
        }
    }
    else if (id == QLatin1String("showData"))
    {
        host_->showStageTab(WorkflowStage::Data, {});
    }
    else if (id == QLatin1String("baseline"))
    {
        if (!ensureProject() || !ensureData())
        {
            return;
        }
        ThesisBaselineDialog dialog(host_->workingData(), example_, host_->hostWindow());
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
        const VarBuilderSpec spec = dialog.spec();
        const QString file = QDir(host_->projectFolder()).filePath(QStringLiteral("baseline.h5"));
        try
        {
            writeVarModel(spec, file);
        }
        catch (const std::exception &e)
        {
            QMessageBox::warning(host_->hostWindow(), tr("Baseline Model"), QString::fromUtf8(e.what()));
            return;
        }
        record_.baseline = specToJson(spec);
        record_.models.removeAll(QStringLiteral("baseline.h5"));
        record_.models.prepend(QStringLiteral("baseline.h5"));
        saveRecord();
        host_->addModel(file);
    }
    else if (id == QLatin1String("builder"))
    {
        host_->openBuilder();
    }
    else if (id == QLatin1String("runAll"))
    {
        if (!record_.models.isEmpty())
        {
            host_->selectModelFile(QDir(folder).filePath(record_.models.front()));
        }
        host_->runAllStages();
    }
    else if (id == QLatin1String("showDraws"))
    {
        host_->showStageTab(WorkflowStage::Posterior, tr("Draws and convergence"));
    }
    else if (id == QLatin1String("lagVariants") || id == QLatin1String("robustnessVariants"))
    {
        const auto spec = baselineSpec();
        if (!spec)
        {
            QMessageBox::information(host_->hostWindow(), tr("Assistant"),
                                     tr("The variants are made from the baseline the assistant created (step 3); "
                                        "create it there, or use the model builder for variants of your own."));
            return;
        }
        const bool lags = id == QLatin1String("lagVariants");
        createAndRun(lags ? lagVariants(*spec) : robustnessVariants(*spec), lags ? QStringLiteral("lags")
                                                                                : QStringLiteral("robustness"));
    }
    else if (id == QLatin1String("showEvaluation"))
    {
        host_->showStageTab(WorkflowStage::Evaluation, tr("Model comparison"));
    }
    else if (id == QLatin1String("showStructural"))
    {
        host_->showStageTab(WorkflowStage::Application, tr("Structural analysis"));
    }
    else if (id == QLatin1String("exampleSigns") && example_)
    {
        const QString file = record_.models.isEmpty() ? host_->currentModel()
                                                      : QDir(folder).filePath(record_.models.front());
        host_->selectModelFile(file);
        const QStringList names = levelNames(file);
        std::vector<SignRestriction> preset;
        for (const ExampleRestriction &r : example_->restrictions)
        {
            const int i = static_cast<int>(names.indexOf(r.impulse));
            const int j = static_cast<int>(names.indexOf(r.response));
            if (i >= 0 && j >= 0)
            {
                preset.push_back({i, j, r.sign, r.horizon, r.threshold});
            }
        }
        host_->showStageTab(WorkflowStage::Application, tr("Structural analysis"));
        host_->openSignRestrictions(preset, example_->signRotated);
    }
    else if (id == QLatin1String("exportThesis"))
    {
        exportThesis();
    }
    else if (id == QLatin1String("designRace"))
    {
        designRace();
    }
    else if (id == QLatin1String("runRace"))
    {
        runRace();
    }
    else if (id == QLatin1String("showRace"))
    {
        host_->showStageTab(WorkflowStage::Evaluation, tr("Horse race"));
    }
    else if (id == QLatin1String("exportRace"))
    {
        exportRace();
    }
    refreshSteps(true);
}

void AssistantPanel::createAndRun(const std::vector<std::pair<QString, VarBuilderSpec>> &variants, const QString &event)
{
    const QDir dir(host_->projectFolder());
    QStringList files;
    for (const auto &[name, spec] : variants)
    {
        const QString file = dir.filePath(name + QStringLiteral(".h5"));
        try
        {
            writeVarModel(spec, file);
        }
        catch (const std::exception &e)
        {
            status_->setText(tr("✗  %1: %2").arg(name, QString::fromUtf8(e.what())));
            continue;
        }
        if (!record_.models.contains(name + QStringLiteral(".h5")))
        {
            record_.models << name + QStringLiteral(".h5");
        }
        files << file;
    }
    // The baseline too, where its log likelihood is missing: the comparison
    // needs it.
    if (!record_.models.isEmpty())
    {
        const QString base = dir.filePath(record_.models.front());
        const ModelFacts f = modelFacts(base);
        if (!f.loglik)
        {
            files.prepend(base);
        }
    }
    saveRecord();
    runFiles(files, event);
}

void AssistantPanel::runFiles(const QStringList &files, const QString &event)
{
    if (batch_->isRunning() || files.isEmpty())
    {
        return;
    }
    setProperty("pendingEvent", event);
    progress_->setRange(0, static_cast<int>(files.size()));
    progress_->setValue(0);
    progress_->setVisible(true);
    status_->setText(tr("Estimating %1 models, several at a time…").arg(files.size()));
    batch_->start(files);
}

void AssistantPanel::exportThesis()
{
    const QDir out(QDir(host_->projectFolder()).filePath(QStringLiteral("thesis-export")));
    out.mkpath(QStringLiteral("figures"));
    out.mkpath(QStringLiteral("tables"));
    QStringList body;
    body << QStringLiteral("% Exported from BayesTS Studio by the thesis assistant.")
         << QStringLiteral("% Needs \\usepackage{graphicx}, \\usepackage{booktabs} and \\usepackage{xcolor}.")
         << QStringLiteral("\\section*{Methods}") << QStringLiteral("\\input{methods}") << QString();

    // The methods paragraph of the model the results come from: the one shown
    // if it carries an identification, else the first that does, else the
    // baseline -- with the baseline's specification, which an identification
    // derived from it shares.
    QString base = record_.models.isEmpty() ? host_->currentModel()
                                            : QDir(host_->projectFolder()).filePath(record_.models.front());
    {
        QStringList candidates{host_->currentModel()};
        candidates << host_->projectModels();
        for (const QString &f : candidates)
        {
            if (!f.isEmpty() && modelFacts(f).sign)
            {
                base = f;
                break;
            }
        }
    }
    if (!base.isEmpty())
    {
        QFile methods(out.filePath(QStringLiteral("methods.tex")));
        if (methods.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            methods.write(methodsParagraph(base, record_.models.isEmpty() ? std::nullopt : baselineSpec()).toUtf8() + "\n");
        }
    }

    int figures = 0;
    QStringList used;
    for (const FigureSpec &spec : figureSources(host_->hostWindow()))
    {
        if (!hasContent(spec))
        {
            continue;
        }
        QString name = stem(spec.name);
        while (used.contains(name))
        {
            name += QStringLiteral("-x");
        }
        used << name;
        double w = 0.0, h = 0.0;
        defaultFigureSize(spec, w, h);
        if (writeFigure(spec, out.filePath(QStringLiteral("figures/%1.pdf").arg(name)), w, h, 300))
        {
            ++figures;
            body << QStringLiteral("\\begin{figure}[htbp]") << QStringLiteral("  \\centering")
                 << QStringLiteral("  \\includegraphics[width=\\textwidth]{figures/%1}").arg(name)
                 << QStringLiteral("  \\caption{%1}").arg(latexText(spec.title.isEmpty() ? spec.name : spec.title))
                 << QStringLiteral("  \\label{fig:%1}").arg(name) << QStringLiteral("\\end{figure}") << QString();
        }
    }
    int tables = 0;
    for (QTableWidget *t : host_->hostWindow()->findChildren<QTableWidget *>())
    {
        const QString caption = t->property("exportCaption").toString();
        if (caption.isEmpty() || t->rowCount() == 0)
        {
            continue;
        }
        QString name = stem(caption);
        while (used.contains(name))
        {
            name += QStringLiteral("-x");
        }
        used << name;
        QFile f(out.filePath(QStringLiteral("tables/%1.tex").arg(name)));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            f.write(toLatex(tableContent(t), name).toUtf8());
            ++tables;
            body << QStringLiteral("\\input{tables/%1}").arg(name) << QString();
        }
    }
    QFile results(out.filePath(QStringLiteral("results.tex")));
    if (results.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        results.write(body.join(QLatin1Char('\n')).toUtf8());
    }
    record_.events << QStringLiteral("exported");
    saveRecord();
    status_->setText(tr("Wrote %1 figures, %2 tables and the methods paragraph into %3; \\input{results} includes "
                        "them all.")
                         .arg(figures)
                         .arg(tables)
                         .arg(QDir::toNativeSeparators(out.path())));
    if (!qEnvironmentVariableIsSet("STUDIO_SMOKE_ASSISTANT"))
        QDesktopServices::openUrl(QUrl::fromLocalFile(out.path()));
}

void AssistantPanel::designRace()
{
    if (!ensureProject() || !ensureData())
    {
        return;
    }
    HorseRaceDialog dialog(host_->workingData(), example_, host_->hostWindow());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const QString folder = QDir(host_->projectFolder()).filePath(QStringLiteral("horse-race"));
    if (QFile::exists(QDir(folder).filePath(QStringLiteral("horse-race.json"))))
    {
        if (QMessageBox::question(host_->hostWindow(), tr("Design the Horse Race"),
                                  tr("The project has a horse race already. Replace it, and every run of it?")) !=
            QMessageBox::Yes)
        {
            return;
        }
        QDir(folder).removeRecursively();
    }
    progress_->setVisible(true);
    status_->setText(tr("Writing the windows…"));
    try
    {
        const HorseRaceManifest m = writeHorseRace(dialog.design(), folder, [this](int done, int total) {
            progress_->setRange(0, total);
            progress_->setValue(done);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            return true;
        });
        status_->setText(tr("Wrote %1 model files into %2.").arg(m.models()).arg(QDir::toNativeSeparators(folder)));
    }
    catch (const std::exception &e)
    {
        QMessageBox::warning(host_->hostWindow(), tr("Design the Horse Race"), QString::fromUtf8(e.what()));
    }
    progress_->setVisible(false);
    if (auto *race = host_->hostWindow()->findChild<HorseRaceView *>())
    {
        race->reload();
    }
}

void AssistantPanel::runRace()
{
    const QString folder = QDir(host_->projectFolder()).filePath(QStringLiteral("horse-race"));
    try
    {
        const HorseRaceManifest m = readHorseRace(folder);
        QStringList files;
        for (const auto &c : m.files)
        {
            for (const QString &f : c)
            {
                if (!hasForecast(f))
                {
                    files << f;
                }
            }
        }
        if (files.isEmpty())
        {
            status_->setText(tr("Every run of the race is done."));
            return;
        }
        runFiles(files, QStringLiteral("raceRun"));
    }
    catch (const std::exception &e)
    {
        QMessageBox::warning(host_->hostWindow(), tr("Run the Horse Race"), QString::fromUtf8(e.what()));
    }
}

void AssistantPanel::exportRace()
{
    auto *race = host_->hostWindow()->findChild<HorseRaceView *>();
    const QString folder = QDir(host_->projectFolder()).filePath(QStringLiteral("horse-race/report"));
    QString error;
    if (!race || !race->exportReport(folder, &error))
    {
        QMessageBox::warning(host_->hostWindow(), tr("Export the Report"), error);
        return;
    }
    record_.events << QStringLiteral("raceReport");
    saveRecord();
    status_->setText(tr("Wrote the league tables and the figure into %1.").arg(QDir::toNativeSeparators(folder)));
    if (!qEnvironmentVariableIsSet("STUDIO_SMOKE_ASSISTANT"))
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

} // namespace studio
