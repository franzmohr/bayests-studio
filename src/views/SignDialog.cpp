#include "views/SignDialog.h"

#include "engine/ModelFileTree.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>

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

enum Column
{
    Shock,
    Response,
    Sign,
    Horizon,
};

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
    resize(720, 520);

    auto *intro = new QLabel(tr(
        "Each row restricts one response to one shock at one horizon: positive, negative or exactly zero. "
        "With signs alone, a rotation of the Choleski factor is drawn for every posterior draw until the "
        "signs hold (Rubio-Ramírez, Waggoner and Zha 2010). A zero restriction needs the importance "
        "sampler of Arias, Rubio-Ramírez and Waggoner (2018), whose result is a reweighted resample of "
        "the posterior and is written to a new model file. Only restricted shocks have an interpretation."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("subtle"));

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({tr("Shock"), tr("Response"), tr("Sign"), tr("Horizon")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *add = new QPushButton(tr("Add Restriction"));
    auto *remove = new QPushButton(tr("Remove"));
    connect(add, &QPushButton::clicked, this, [this]() {
        const int last = table_->rowCount() - 1;
        SignRestriction r;
        if (last >= 0)
        {
            r.impulse = qobject_cast<QComboBox *>(table_->cellWidget(last, Shock))->currentIndex();
        }
        addRow(r);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = table_->currentRow();
        if (row >= 0)
        {
            table_->removeRow(row);
        }
    });
    auto *rowButtons = new QHBoxLayout;
    rowButtons->addWidget(add);
    rowButtons->addWidget(remove);
    rowButtons->addStretch(1);

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
    layout->addWidget(table_, 1);
    layout->addLayout(rowButtons);
    layout->addLayout(settings);
    layout->addWidget(mode_);
    layout->addWidget(arwBox_);
    layout->addWidget(progress_);
    layout->addWidget(status_);
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

    // What the model carries already, or a first row to start from.
    if (support.sign)
    {
        for (const SignRestriction &r : support.sign->restrictions)
        {
            addRow(r);
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
    else
    {
        addRow(SignRestriction{});
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

void SignDialog::addRow(const SignRestriction &r)
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto *shock = new QComboBox;
    shock->addItems(support_.variables);
    shock->setCurrentIndex(r.impulse);
    auto *response = new QComboBox;
    response->addItems(support_.variables);
    response->setCurrentIndex(r.response);
    auto *sign = new QComboBox;
    sign->addItem(tr("+  positive"), 1);
    sign->addItem(tr("−  negative"), -1);
    sign->addItem(tr("0  zero"), 0);
    sign->setCurrentIndex(r.sign > 0 ? 0 : (r.sign < 0 ? 1 : 2));
    auto *horizon = new QSpinBox;
    horizon->setRange(-1, 200);
    horizon->setSpecialValueText(tr("long run"));
    horizon->setValue(r.horizon);
    connect(sign, &QComboBox::currentIndexChanged, this, &SignDialog::updateMode);
    connect(horizon, &QSpinBox::valueChanged, this, &SignDialog::updateMode);
    table_->setCellWidget(row, Shock, shock);
    table_->setCellWidget(row, Response, response);
    table_->setCellWidget(row, Sign, sign);
    table_->setCellWidget(row, Horizon, horizon);
    updateMode();
}

bool SignDialog::hasZero() const
{
    for (int row = 0; row < table_->rowCount(); ++row)
    {
        if (qobject_cast<QComboBox *>(table_->cellWidget(row, Sign))->currentData().toInt() == 0)
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
}

SignIdentification SignDialog::readTable() const
{
    SignIdentification id;
    for (int row = 0; row < table_->rowCount(); ++row)
    {
        SignRestriction r;
        r.impulse = qobject_cast<QComboBox *>(table_->cellWidget(row, Shock))->currentIndex();
        r.response = qobject_cast<QComboBox *>(table_->cellWidget(row, Response))->currentIndex();
        r.sign = qobject_cast<QComboBox *>(table_->cellWidget(row, Sign))->currentData().toInt();
        r.horizon = qobject_cast<QSpinBox *>(table_->cellWidget(row, Horizon))->value();
        id.restrictions.push_back(r);
    }
    id.maxTries = maxTries_->value();
    id.period = support_.moving ? period_->value() : -1;
    return id;
}

void SignDialog::identify()
{
    const SignIdentification spec = readTable();
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
