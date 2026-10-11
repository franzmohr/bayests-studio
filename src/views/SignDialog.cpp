#include "views/SignDialog.h"

#include "engine/ModelFileTree.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QTabBar>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <highfive/highfive.hpp>

namespace studio
{

namespace
{

/// /model/seed, or nothing: the search is then seeded at random.
std::optional<std::uint64_t> modelSeed(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::Group g = h5.getGroup("/model");
        if (g.hasAttribute("seed"))
        {
            const auto v = g.getAttribute("seed").read<std::vector<double>>();
            if (!v.empty())
            {
                return static_cast<std::uint64_t>(v.front());
            }
        }
    }
    catch (const std::exception &)
    {
    }
    return std::nullopt;
}

} // namespace

SignDialog::SignDialog(const QString &file, const StructuralSupport &support, QWidget *parent)
    : QDialog(parent), file_(file), support_(support)
{
    setWindowTitle(tr("Sign Restrictions"));
    resize(860, 720);

    auto *intro = new QLabel(tr(
        "Restrict the response of each variable (rows) to each shock (columns) at a horizon: click a cell "
        "to step it through +, − and 0, or type +, - or 0 (Space steps, Delete frees it). With signs alone, "
        "a rotation of the Choleski factor is drawn for every posterior draw until the signs hold "
        "(Rubio-Ramírez, Waggoner and Zha 2010). A zero needs the importance sampler of Arias, "
        "Rubio-Ramírez and Waggoner (2018), whose result is a reweighted resample of the posterior and "
        "is written to a new model file. Only restricted shocks have an interpretation."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("subtle"));

    // One grid per horizon, a tab each.
    horizons_ = new QTabBar;
    horizons_->setObjectName(QStringLiteral("signHorizons"));
    horizons_->setExpanding(false);
    horizons_->setDrawBase(false);
    connect(horizons_, &QTabBar::currentChanged, this, &SignDialog::showGrid);
    auto *addHorizon = new QPushButton(tr("Add Horizon…"));
    auto *removeHorizon = new QPushButton(tr("Remove Horizon"));
    connect(addHorizon, &QPushButton::clicked, this, &SignDialog::addHorizon);
    connect(removeHorizon, &QPushButton::clicked, this, &SignDialog::removeHorizon);
    auto *horizonBar = new QHBoxLayout;
    horizonBar->addWidget(horizons_, 1);
    horizonBar->addWidget(addHorizon);
    horizonBar->addWidget(removeHorizon);

    const int k = support.k;
    grid_ = new QTableWidget(k, k);
    grid_->setObjectName(QStringLiteral("signGrid"));
    QStringList shocks;
    for (const QString &v : support.variables)
    {
        shocks << tr("%1 shock").arg(v);
    }
    grid_->setHorizontalHeaderLabels(shocks);
    grid_->setVerticalHeaderLabels(support.variables);
    grid_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    grid_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    grid_->verticalHeader()->setDefaultSectionSize(40);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setSelectionMode(QAbstractItemView::NoSelection);
    // No selection, so a cell's colour always shows; the focus frame marks
    // the current cell for the keys. As tall as its rows, up to eight.
    const int visibleRows = std::min(k, 8);
    grid_->setFixedHeight(grid_->horizontalHeader()->sizeHint().height() + 40 * visibleRows + 2 * grid_->frameWidth() + 2);
    for (int r = 0; r < k; ++r)
    {
        for (int c = 0; c < k; ++c)
        {
            auto *item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignCenter);
            item->setToolTip(tr("Response of %1 to the %2 shock: click to step through +, − and 0")
                                 .arg(support.variables.value(r), support.variables.value(c)));
            grid_->setItem(r, c, item);
        }
    }
    grid_->installEventFilter(this);
    connect(grid_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        const int now = signs_[currentHorizon()][static_cast<std::size_t>(r * support_.k + c)];
        // Free -> + -> - -> 0 -> free.
        const int next = now == Free ? 1 : (now == 1 ? -1 : (now == -1 ? 0 : Free));
        setSign(currentHorizon(), r, c, next);
    });
    auto *axes = new QLabel(tr("Rows: responses · Columns: shocks"));
    axes->setObjectName(QStringLiteral("subtle"));
    summary_ = new QLabel;
    summary_->setObjectName(QStringLiteral("subtle"));
    summary_->setWordWrap(true);

    maxTries_ = new QSpinBox;
    maxTries_->setRange(1, 1000000);
    maxTries_->setValue(1000);
    maxTries_->setSingleStep(500);
    period_ = new QSpinBox;
    period_->setRange(1, std::max(1, support.periods));
    period_->setValue(std::max(1, support.periods));
    auto *settings = new QFormLayout;
    settings->addRow(tr("Tries per draw"), maxTries_);
    if (support.moving)
    {
        settings->addRow(tr("Period"), period_);
    }

    mode_ = new QLabel;
    mode_->setObjectName(QStringLiteral("sectionTitle"));

    arwBox_ = new QGroupBox(tr("Sign and zero restrictions (Arias, Rubio-Ramírez and Waggoner 2018)"));
    auto *arw = new QFormLayout(arwBox_);
    arwTries_ = new QSpinBox;
    arwTries_->setRange(1, 100000);
    arwTries_->setValue(1);
    arwTries_->setToolTip(tr("One is the paper's algorithm. More tries find rotations where the signs leave "
                             "a small region, and the weights account for them."));
    smooth_ = new QCheckBox(tr("Pareto smooth the importance weights (Vehtari et al. 2024)"));
    smooth_->setChecked(true);
    oneSided_ = new QCheckBox(tr("One-sided numerical derivative (faster, less accurate)"));
    resample_ = new QSpinBox;
    resample_->setRange(0, 10000000);
    resample_->setSpecialValueText(tr("the effective sample size"));
    const QFileInfo source(file);
    output_ = new QLineEdit(source.dir().filePath(source.completeBaseName() + QStringLiteral("-signzero.h5")));
    arw->addRow(tr("Rotations per draw"), arwTries_);
    arw->addRow(QString(), smooth_);
    arw->addRow(QString(), oneSided_);
    arw->addRow(tr("Draws to resample"), resample_);
    arw->addRow(tr("Write to"), output_);

    progress_ = new QProgressBar;
    progress_->setVisible(false);
    status_ = new QLabel;
    status_->setWordWrap(true);

    auto *buttons = new QDialogButtonBox;
    identify_ = buttons->addButton(tr("Identify"), QDialogButtonBox::ActionRole);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    identify_->setDefault(true);
    connect(identify_, &QPushButton::clicked, this, &SignDialog::identify);
    connect(cancel_, &QPushButton::clicked, this, [this]() {
        if (watcher_->isRunning())
        {
            cancelled_.store(true);
        }
        else
        {
            QDialog::reject();
        }
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(horizonBar);
    layout->addWidget(grid_);
    layout->addWidget(axes);
    layout->addWidget(summary_);
    layout->addLayout(settings);
    layout->addWidget(mode_);
    layout->addWidget(arwBox_);
    layout->addWidget(progress_);
    layout->addWidget(status_);
    layout->addStretch(1);
    layout->addWidget(buttons);

    watcher_ = new QFutureWatcher<Outcome>(this);
    connect(watcher_, &QFutureWatcher<Outcome>::finished, this, [this]() {
        const Outcome o = watcher_->result();
        identify_->setEnabled(true);
        progress_->setVisible(false);
        if (!o.error.isEmpty())
        {
            status_->setText(tr("✗  %1").arg(o.error));
            status_->setStyleSheet(QStringLiteral("color: #B3261E;"));
            return;
        }
        if (o.zero)
        {
            const SignZeroResult &r = o.signZero;
            created_ = output_->text();
            QString text = tr("✓  %1 of %2 draws satisfied the signs; effective sample size %3, Pareto k %4. "
                              "%5 draws resampled into %6.")
                               .arg(r.accepted)
                               .arg(r.candidates)
                               .arg(r.effectiveSampleSize)
                               .arg(std::isfinite(r.paretoK) ? QString::number(r.paretoK, 'f', 2) : tr("—"))
                               .arg(r.draws)
                               .arg(QFileInfo(created_).fileName());
            if (!r.warnings.isEmpty())
            {
                // An unreliable sample is written all the same, as bvartools
                // writes it, but the dialog stays open on the warning.
                status_->setText(text + "\n\n⚠  " + r.warnings.join(QStringLiteral("\n\n⚠  ")));
                status_->setStyleSheet(QStringLiteral("color: #8A5300;"));
                identify_->setText(tr("Done"));
                disconnect(identify_, nullptr, this, nullptr);
                connect(identify_, &QPushButton::clicked, this, &QDialog::accept);
                return;
            }
            status_->setText(text);
        }
        else
        {
            status_->setText(tr("✓  %1 of %2 draws identified.").arg(o.identification.accepted).arg(o.identification.draws));
        }
        status_->setStyleSheet(QStringLiteral("color: #1E7B4A;"));
        accept();
    });

    // What the model carries already, or impact with the first variable's
    // own response positive to start from.
    if (support.sign)
    {
        for (const SignRestriction &r : support.sign->restrictions)
        {
            setSign(r.horizon < 0 ? -1 : r.horizon, r.response, r.impulse, r.sign);
        }
        maxTries_->setValue(support.sign->maxTries);
        if (support.sign->period > 0)
        {
            period_->setValue(support.sign->period);
        }
        status_->setText(tr("The model carries this identification: %1 of %2 draws identified. "
                            "Identifying again replaces it.")
                             .arg(support.sign->accepted)
                             .arg(support.sign->draws));
    }
    else if (support.k > 0)
    {
        setSign(0, 0, 0, 1);
    }
    rebuildTabs(signs_.count(0) ? 0 : (signs_.empty() ? 0 : signs_.begin()->first));
    if (signs_.empty())
    {
        signs_[0].assign(static_cast<std::size_t>(support.k * support.k), Free);
        rebuildTabs(0);
    }
}

void SignDialog::reject()
{
    if (watcher_->isRunning())
    {
        cancelled_.store(true);
        watcher_->waitForFinished();
    }
    QDialog::reject();
}

namespace
{

/// Impact first, then the horizons in order, the long run (-1) last.
std::vector<int> horizonOrder(const std::map<int, std::vector<int>> &signs)
{
    std::vector<int> order;
    for (const auto &[h, cells] : signs)
    {
        if (h >= 0)
        {
            order.push_back(h);
        }
    }
    if (signs.count(-1))
    {
        order.push_back(-1);
    }
    return order;
}

QString horizonLabel(int h)
{
    if (h < 0)
    {
        return QObject::tr("Long run");
    }
    return h == 0 ? QObject::tr("Impact") : QObject::tr("Horizon %1").arg(h);
}

} // namespace

void SignDialog::setSign(int horizon, int response, int shock, int sign)
{
    const int k = support_.k;
    if (response < 0 || response >= k || shock < 0 || shock >= k)
    {
        return;
    }
    std::vector<int> &cells = signs_[horizon];
    if (cells.empty())
    {
        cells.assign(static_cast<std::size_t>(k * k), Free);
    }
    cells[static_cast<std::size_t>(response * k + shock)] = sign;
    if (horizon == currentHorizon())
    {
        paintCell(response, shock);
    }
    // The tab counts its restrictions.
    for (int t = 0; t < horizons_->count(); ++t)
    {
        const int h = horizons_->tabData(t).toInt();
        const auto n = std::count_if(signs_[h].begin(), signs_[h].end(), [](int v) { return v != Free; });
        horizons_->setTabText(t, n > 0 ? tr("%1 (%2)").arg(horizonLabel(h)).arg(n) : horizonLabel(h));
    }
    updateMode();
}

void SignDialog::rebuildTabs(int select)
{
    const QSignalBlocker block(horizons_);
    while (horizons_->count() > 0)
    {
        horizons_->removeTab(0);
    }
    int current = 0;
    for (const int h : horizonOrder(signs_))
    {
        const int t = horizons_->addTab(horizonLabel(h));
        horizons_->setTabData(t, h);
        if (h == select)
        {
            current = t;
        }
    }
    horizons_->setCurrentIndex(current);
    for (int t = 0; t < horizons_->count(); ++t)
    {
        const int h = horizons_->tabData(t).toInt();
        const auto n = std::count_if(signs_[h].begin(), signs_[h].end(), [](int v) { return v != Free; });
        horizons_->setTabText(t, n > 0 ? tr("%1 (%2)").arg(horizonLabel(h)).arg(n) : horizonLabel(h));
    }
    showGrid();
}

int SignDialog::currentHorizon() const
{
    const int t = horizons_->currentIndex();
    return t < 0 ? 0 : horizons_->tabData(t).toInt();
}

void SignDialog::showGrid()
{
    for (int r = 0; r < support_.k; ++r)
    {
        for (int c = 0; c < support_.k; ++c)
        {
            paintCell(r, c);
        }
    }
}

void SignDialog::paintCell(int response, int shock)
{
    QTableWidgetItem *item = grid_->item(response, shock);
    const auto it = signs_.find(currentHorizon());
    if (item == nullptr || it == signs_.end() || it->second.empty())
    {
        return;
    }
    const int v = it->second[static_cast<std::size_t>(response * support_.k + shock)];
    QFont f = item->font();
    f.setBold(true);
    f.setPointSizeF(QFont().pointSizeF() * 1.35);
    item->setFont(f);
    switch (v)
    {
    case 1:
        item->setText(QStringLiteral("+"));
        item->setBackground(QColor(30, 123, 74, 70));
        break;
    case -1:
        item->setText(QStringLiteral("−"));
        item->setBackground(QColor(179, 38, 30, 70));
        break;
    case 0:
        item->setText(QStringLiteral("0"));
        item->setBackground(QColor(90, 110, 140, 70));
        break;
    default:
        item->setText(QString());
        item->setBackground(QBrush());
        break;
    }
}

void SignDialog::addHorizon()
{
    QMenu menu(this);
    QAction *at = menu.addAction(tr("At a horizon…"));
    QAction *longRun = menu.addAction(tr("In the long run"));
    longRun->setEnabled(signs_.count(-1) == 0);
    longRun->setToolTip(tr("A long-run restriction needs the sign and zero sampler."));
    QAction *chosen = menu.exec(QCursor::pos());
    int h = 0;
    if (chosen == longRun)
    {
        h = -1;
    }
    else if (chosen == at)
    {
        int next = 1;
        while (signs_.count(next))
        {
            ++next;
        }
        bool ok = false;
        h = QInputDialog::getInt(this, tr("Add Horizon"), tr("Periods after impact"), next, 0, 200, 1, &ok);
        if (!ok)
        {
            return;
        }
    }
    else
    {
        return;
    }
    if (!signs_.count(h))
    {
        signs_[h].assign(static_cast<std::size_t>(support_.k * support_.k), Free);
    }
    rebuildTabs(h);
}

void SignDialog::removeHorizon()
{
    signs_.erase(currentHorizon());
    if (signs_.empty())
    {
        signs_[0].assign(static_cast<std::size_t>(support_.k * support_.k), Free);
    }
    rebuildTabs(signs_.begin()->first >= 0 ? signs_.begin()->first : -1);
    updateMode();
}

bool SignDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == grid_ && event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int r = grid_->currentRow();
        const int c = grid_->currentColumn();
        if (r >= 0 && c >= 0)
        {
            const QString text = key->text();
            int sign = 3;   // no change
            if (text == QLatin1String("+") || text == QLatin1String("p"))
            {
                sign = 1;
            }
            else if (text == QLatin1String("-") || text == QStringLiteral("−") || text == QLatin1String("n"))
            {
                sign = -1;
            }
            else if (text == QLatin1String("0"))
            {
                sign = 0;
            }
            else if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace)
            {
                sign = Free;
            }
            else if (key->key() == Qt::Key_Space)
            {
                emit grid_->cellClicked(r, c);
                return true;
            }
            if (sign != 3)
            {
                setSign(currentHorizon(), r, c, sign);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SignDialog::preset(const std::vector<SignRestriction> &restrictions)
{
    signs_.clear();
    for (const SignRestriction &r : restrictions)
    {
        setSign(r.horizon < 0 ? -1 : r.horizon, r.response, r.impulse, r.sign);
    }
    if (signs_.empty())
    {
        signs_[0].assign(static_cast<std::size_t>(support_.k * support_.k), Free);
    }
    rebuildTabs(signs_.begin()->first);
    updateMode();
}

bool SignDialog::hasZero() const
{
    for (const auto &[h, cells] : signs_)
    {
        if (std::find(cells.begin(), cells.end(), 0) != cells.end())
        {
            return true;
        }
    }
    return false;
}

void SignDialog::updateMode()
{
    const bool zero = hasZero();
    arwBox_->setVisible(zero);
    maxTries_->setEnabled(!zero);
    mode_->setText(zero ? tr("A zero restriction: the importance sampler, into a new model file.")
                        : tr("Signs alone: rotations tried per draw, written into this model."));
    const SignIdentification id = readTable();
    QStringList restricted;
    for (int shock = 0; shock < support_.k; ++shock)
    {
        for (const SignRestriction &r : id.restrictions)
        {
            if (r.impulse == shock)
            {
                restricted << support_.variables.value(shock);
                break;
            }
        }
    }
    summary_->setText(id.restrictions.empty()
                          ? tr("No restriction set yet.")
                          : tr("%n restriction(s); identified shocks: %1.", nullptr,
                               static_cast<int>(id.restrictions.size()))
                                .arg(restricted.join(QStringLiteral(", "))));
}

SignIdentification SignDialog::readTable() const
{
    SignIdentification id;
    const int k = support_.k;
    // Shock by shock, then horizon and response: the order bvartools' table
    // lists a shock's restrictions in.
    for (int shock = 0; shock < k; ++shock)
    {
        for (const int h : horizonOrder(signs_))
        {
            const std::vector<int> &cells = signs_.at(h);
            for (int response = 0; response < k; ++response)
            {
                const int v = cells.empty() ? Free : cells[static_cast<std::size_t>(response * k + shock)];
                if (v != Free)
                {
                    id.restrictions.push_back({shock, response, v, h});
                }
            }
        }
    }
    id.maxTries = maxTries_->value();
    id.period = support_.moving ? period_->value() : -1;
    return id;
}

void SignDialog::identify()
{
    const SignIdentification spec = readTable();
    if (spec.restrictions.empty())
    {
        status_->setText(tr("✗  Set at least one cell to +, − or 0."));
        status_->setStyleSheet(QStringLiteral("color: #B3261E;"));
        return;
    }
    const bool zero = hasZero();
    for (const SignRestriction &r : spec.restrictions)
    {
        if (r.horizon < 0 && !zero)
        {
            status_->setText(tr("✗  A long-run restriction needs the sign and zero sampler, which needs at least "
                                "one zero restriction."));
            status_->setStyleSheet(QStringLiteral("color: #B3261E;"));
            return;
        }
    }
    SignZeroSpec zspec;
    for (const SignRestriction &r : spec.restrictions)
    {
        zspec.restrictions.push_back({r.impulse, r.response, r.sign,
                                      r.horizon < 0 ? std::numeric_limits<double>::infinity() : double(r.horizon)});
    }
    zspec.maxTries = arwTries_->value();
    zspec.smooth = smooth_->isChecked();
    zspec.oneSided = oneSided_->isChecked();
    zspec.period = spec.period;
    zspec.draws = resample_->value();
    const QString output = output_->text();
    if (zero && QFileInfo(output).absoluteFilePath() == QFileInfo(file_).absoluteFilePath())
    {
        status_->setText(tr("✗  Write the resample to a file of its own; the model's posterior is kept as it is."));
        status_->setStyleSheet(QStringLiteral("color: #B3261E;"));
        return;
    }
    // Reproducible from the model's own seed where it has one, on a stream of
    // its own so it does not repeat the sampler's.
    const std::optional<std::uint64_t> base = modelSeed(file_);
    const std::uint64_t seed = base ? (*base ^ 0x5349474E5349474EULL)
                                    : QRandomGenerator::global()->generate64();
    cancelled_.store(false);
    identify_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setRange(0, 0);
    status_->setText(tr("Searching rotations…"));
    status_->setStyleSheet({});

    const QString file = file_;
    QProgressBar *bar = progress_;
    std::atomic<bool> *cancelled = &cancelled_;
    watcher_->setFuture(QtConcurrent::run([file, spec, zspec, zero, output, seed, bar, cancelled]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Outcome o;
        o.zero = zero;
        const auto progress = [bar, cancelled](int done, int total) {
            QMetaObject::invokeMethod(bar, [bar, done, total]() {
                bar->setRange(0, total);
                bar->setValue(done);
            }, Qt::QueuedConnection);
            return !cancelled->load();
        };
        try
        {
            if (zero)
            {
                o.signZero = identifySignZero(file, output, zspec, seed, progress);
            }
            else
            {
                o.identification = identifySigns(file, spec, seed, progress);
            }
        }
        catch (const std::exception &e)
        {
            o.error = QString::fromUtf8(e.what());
        }
        return o;
    }));
}

} // namespace studio
