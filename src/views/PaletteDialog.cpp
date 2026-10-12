#include "views/PaletteDialog.h"

#include "views/Palettes.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{

/// A strip of the palette's first colours, as six series would get them.
QPixmap strip(const Palette &p, int width, int height, int count = 6)
{
    QPixmap pix(width, height);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    const int n = p.kind == Palette::Kind::Qualitative && !p.colours.empty()
                      ? std::min<int>(static_cast<int>(p.colours.size()), 10)
                      : count;
    const double w = static_cast<double>(width) / n;
    for (int i = 0; i < n; ++i)
    {
        painter.fillRect(QRectF(i * w, 0, w + 0.5, height), p.colour(i, n));
    }
    return pix;
}

/// Lines in the palette, as a plot of several series would draw them.
QPixmap lines(const Palette &p, int count)
{
    QPixmap pix(420, 120);
    pix.fill(Qt::white);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int s = 0; s < count; ++s)
    {
        QPainterPath path;
        for (int x = 0; x <= 420; x += 6)
        {
            const double y = 60 + 38 * std::sin(x / 55.0 + s * 0.9) * (0.4 + 0.12 * s) + (s - count / 2.0) * 6;
            if (x == 0)
            {
                path.moveTo(x, y);
            }
            else
            {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(p.colour(s, count), 2.4));
        painter.drawPath(path);
    }
    return pix;
}

/// One of the user's palettes as the registry lists it.
Palette asPalette(const OwnPalette &own)
{
    Palette p;
    p.id = ownPaletteId(own.name);
    p.name = own.name;
    p.family = ownPaletteFamily();
    p.source = QObject::tr("Your own colours");
    p.kind = Palette::Kind::Qualitative;
    p.colours = own.colours;
    return p;
}

constexpr int kOwnRole = Qt::UserRole + 1;   ///< index into own_, on an own palette's item

} // namespace

PaletteDialog::PaletteDialog(QWidget *parent) : QDialog(parent), own_(ownPalettes())
{
    setWindowTitle(tr("Colour Palette"));
    resize(640, 720);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("The colours every plot draws its series from, and every exported figure. The "
                                "studio's default is Okabe and Ito's, safe for the common colour vision "
                                "deficiencies. Under My organisation's colours keep as many palettes of your own "
                                "as you need -- a house style, a university's, a journal's."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("subtle"));
    layout->addWidget(intro);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("paletteList"));
    list_->setIconSize(QSize(160, 16));
    list_->setMinimumHeight(260);
    list_->setToolTip(tr("The palettes, by the package they come from. The studio's default is safe for the common "
                         "colour vision deficiencies."));
    layout->addWidget(list_, 1);

    auto *newOwn = new QPushButton(tr("New Palette…"));
    newOwn->setObjectName(QStringLiteral("newPalette"));
    newOwn->setToolTip(tr("A palette of your own, starting from the colours of the one selected."));
    rename_ = new QPushButton(tr("Rename…"));
    rename_->setToolTip(tr("Renames the palette of your own selected."));
    delete_ = new QPushButton(tr("Delete"));
    delete_->setToolTip(tr("Deletes the palette of your own selected."));
    connect(newOwn, &QPushButton::clicked, this, &PaletteDialog::addOwn);
    connect(rename_, &QPushButton::clicked, this, &PaletteDialog::renameOwn);
    connect(delete_, &QPushButton::clicked, this, &PaletteDialog::deleteOwn);
    auto *ownRow = new QHBoxLayout;
    ownRow->addWidget(newOwn);
    ownRow->addWidget(rename_);
    ownRow->addWidget(delete_);
    ownRow->addStretch(1);
    layout->addLayout(ownRow);

    source_ = new QLabel;
    source_->setObjectName(QStringLiteral("subtle"));
    source_->setWordWrap(true);
    layout->addWidget(source_);
    preview_ = new QLabel;
    preview_->setAlignment(Qt::AlignCenter);
    layout->addWidget(preview_);

    // The colours of the own palette selected.
    editor_ = new QWidget;
    auto *el = new QVBoxLayout(editor_);
    el->setContentsMargins(0, 0, 0, 0);
    el->addWidget(new QLabel(tr("Click a colour to change it.")));
    swatches_ = new QHBoxLayout;
    el->addLayout(swatches_);
    auto *add = new QPushButton(tr("Add a Colour"));
    add->setToolTip(tr("Adds a colour at the end: the next series drawn takes it."));
    auto *remove = new QPushButton(tr("Remove the Last"));
    remove->setToolTip(tr("Removes the last colour; a palette keeps at least one."));
    connect(add, &QPushButton::clicked, this, [this]() {
        const int i = ownIndex();
        const QColor c = i >= 0 ? QColorDialog::getColor(Qt::gray, this, tr("Add a Colour")) : QColor();
        if (c.isValid())
        {
            own_[static_cast<std::size_t>(i)].colours.push_back(c);
            rebuildSwatches();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int i = ownIndex();
        if (i >= 0 && own_[static_cast<std::size_t>(i)].colours.size() > 1)
        {
            own_[static_cast<std::size_t>(i)].colours.pop_back();
            rebuildSwatches();
        }
    });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    el->addLayout(buttons);
    layout->addWidget(editor_);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
    connect(list_, &QListWidget::currentItemChanged, this, &PaletteDialog::showChosen);
    rebuildList(currentPalette().id);
}

void PaletteDialog::rebuildList(const QString &select)
{
    {
        const QSignalBlocker block(list_);
        list_->clear();
        const auto header = [this](const QString &family) {
            auto *item = new QListWidgetItem(family);
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setFlags(Qt::NoItemFlags);
            list_->addItem(item);
        };
        QListWidgetItem *chosen = nullptr;
        QString family;
        for (const Palette &p : palettes())
        {
            if (p.family == ownPaletteFamily())
            {
                continue;   // listed from the dialog's own copy, below
            }
            if (p.family != family)
            {
                family = p.family;
                header(family);
            }
            auto *item = new QListWidgetItem(QIcon(strip(p, 160, 16)), p.name);
            item->setData(Qt::UserRole, p.id);
            item->setToolTip(p.source);
            list_->addItem(item);
            chosen = p.id == select ? item : chosen;
        }
        header(ownPaletteFamily());
        for (std::size_t i = 0; i < own_.size(); ++i)
        {
            const Palette p = asPalette(own_[i]);
            auto *item = new QListWidgetItem(QIcon(strip(p, 160, 16)), p.name);
            item->setData(Qt::UserRole, p.id);
            item->setData(kOwnRole, static_cast<int>(i));
            item->setToolTip(tr("One of your own palettes: rename, delete or edit it below."));
            list_->addItem(item);
            chosen = p.id == select ? item : chosen;
        }
        if (own_.empty())
        {
            auto *hint = new QListWidgetItem(tr("None yet: New Palette… makes one."));
            hint->setFlags(Qt::NoItemFlags);
            list_->addItem(hint);
        }
        for (int r = 0; r < list_->count() && !chosen; ++r)
        {
            chosen = (list_->item(r)->flags() & Qt::ItemIsSelectable) ? list_->item(r) : nullptr;
        }
        list_->setCurrentItem(chosen);
        // Once laid out: before the dialog is shown there is nothing to scroll.
        QTimer::singleShot(0, list_, [list = list_]() {
            if (QListWidgetItem *item = list->currentItem())
            {
                list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            }
        });
    }
    showChosen();
}

QString PaletteDialog::chosen() const
{
    const QListWidgetItem *item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QStringLiteral("studio");
}

int PaletteDialog::ownIndex() const
{
    const QListWidgetItem *item = list_->currentItem();
    return item && item->data(kOwnRole).isValid() ? item->data(kOwnRole).toInt() : -1;
}

void PaletteDialog::showChosen()
{
    const int i = ownIndex();
    rename_->setEnabled(i >= 0);
    delete_->setEnabled(i >= 0);
    editor_->setVisible(i >= 0);
    if (i >= 0)
    {
        const Palette p = asPalette(own_[static_cast<std::size_t>(i)]);
        source_->setText(tr("%1 -- %2").arg(p.name, p.source));
        rebuildSwatches();
        return;
    }
    const QString id = chosen();
    for (const Palette &p : palettes())
    {
        if (p.id == id)
        {
            source_->setText(tr("%1 -- %2").arg(p.name, p.source));
            preview_->setPixmap(lines(p, 5));
        }
    }
}

void PaletteDialog::rebuildSwatches()
{
    while (QLayoutItem *item = swatches_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    const int i = ownIndex();
    if (i < 0)
    {
        return;
    }
    const std::vector<QColor> &colours = own_[static_cast<std::size_t>(i)].colours;
    for (std::size_t c = 0; c < colours.size(); ++c)
    {
        auto *b = new QToolButton;
        b->setFixedSize(34, 26);
        b->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(colours[c].name()));
        b->setToolTip(tr("Series %1: %2. Click to change it.").arg(c + 1).arg(colours[c].name()));
        connect(b, &QToolButton::clicked, this, [this, i, c]() {
            std::vector<QColor> &cs = own_[static_cast<std::size_t>(i)].colours;
            const QColor picked = QColorDialog::getColor(cs[c], this, tr("Colour %1").arg(c + 1));
            if (picked.isValid())
            {
                cs[c] = picked;
                rebuildSwatches();
            }
        });
        swatches_->addWidget(b);
    }
    swatches_->addStretch(1);
    // The preview and the list's strip follow the edit.
    const Palette p = asPalette(own_[static_cast<std::size_t>(i)]);
    preview_->setPixmap(lines(p, 5));
    if (QListWidgetItem *item = list_->currentItem())
    {
        item->setIcon(QIcon(strip(p, 160, 16)));
    }
}

QString PaletteDialog::askName(const QString &title, const QString &current)
{
    for (;;)
    {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, title, tr("Name"), QLineEdit::Normal, current, &ok).trimmed();
        if (!ok || name.isEmpty())
        {
            return {};
        }
        const bool taken = std::any_of(own_.begin(), own_.end(), [&](const OwnPalette &p) { return p.name == name; });
        if (!taken || name == current)
        {
            return name;
        }
        QMessageBox::information(this, title, tr("There is a palette called \"%1\" already.").arg(name));
    }
}

void PaletteDialog::addOwn()
{
    const QString name = askName(tr("New Palette"), QString());
    if (name.isEmpty())
    {
        return;
    }
    // From the colours of the palette selected: all of a list, six of a map.
    OwnPalette p;
    p.name = name;
    if (const int i = ownIndex(); i >= 0)
    {
        p.colours = own_[static_cast<std::size_t>(i)].colours;
    }
    else
    {
        const QString id = chosen();
        for (const Palette &x : palettes())
        {
            if (x.id == id)
            {
                const int n = x.kind == Palette::Kind::Qualitative && !x.colours.empty()
                                  ? static_cast<int>(x.colours.size())
                                  : 6;
                for (int k = 0; k < n; ++k)
                {
                    p.colours.push_back(x.colour(k, n));
                }
            }
        }
    }
    if (p.colours.empty())
    {
        p.colours = {QColor(0x2F, 0x6D, 0xB5), QColor(0xD5, 0x5E, 0x00), QColor(0x00, 0x9E, 0x73)};
    }
    own_.push_back(std::move(p));
    rebuildList(ownPaletteId(name));
}

void PaletteDialog::renameOwn()
{
    const int i = ownIndex();
    if (i < 0)
    {
        return;
    }
    OwnPalette &p = own_[static_cast<std::size_t>(i)];
    const QString name = askName(tr("Rename the Palette"), p.name);
    if (!name.isEmpty())
    {
        p.name = name;
        rebuildList(ownPaletteId(name));
    }
}

void PaletteDialog::deleteOwn()
{
    const int i = ownIndex();
    if (i < 0)
    {
        return;
    }
    own_.erase(own_.begin() + i);
    rebuildList(own_.empty() ? QStringLiteral("studio")
                             : ownPaletteId(own_[static_cast<std::size_t>(std::max(0, i - 1))].name));
}

} // namespace studio
