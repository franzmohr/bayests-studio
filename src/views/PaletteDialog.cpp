#include "views/PaletteDialog.h"

#include "views/Palettes.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

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

} // namespace

PaletteDialog::PaletteDialog(QWidget *parent) : QDialog(parent), custom_(customColours())
{
    setWindowTitle(tr("Colour Palette"));
    resize(640, 560);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("The colours every plot draws its series from, and every exported figure. The "
                                "studio's default is Okabe and Ito's, safe for the common colour vision "
                                "deficiencies."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("subtle"));
    layout->addWidget(intro);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("paletteList"));
    list_->setIconSize(QSize(160, 16));
    QString family;
    const QString current = currentPalette().id;
    for (const Palette &p : palettes())
    {
        if (p.family != family)
        {
            family = p.family;
            auto *header = new QListWidgetItem(family);
            QFont f = header->font();
            f.setBold(true);
            header->setFont(f);
            header->setFlags(Qt::NoItemFlags);
            list_->addItem(header);
        }
        auto *item = new QListWidgetItem(QIcon(strip(p, 160, 16)), p.name);
        item->setData(Qt::UserRole, p.id);
        list_->addItem(item);
        if (p.id == current)
        {
            list_->setCurrentItem(item);
        }
    }
    layout->addWidget(list_, 1);

    source_ = new QLabel;
    source_->setObjectName(QStringLiteral("subtle"));
    source_->setWordWrap(true);
    layout->addWidget(source_);
    preview_ = new QLabel;
    preview_->setAlignment(Qt::AlignCenter);
    layout->addWidget(preview_);

    // The user's own colours.
    editor_ = new QWidget;
    auto *el = new QVBoxLayout(editor_);
    el->setContentsMargins(0, 0, 0, 0);
    el->addWidget(new QLabel(tr("Click a colour to change it.")));
    swatches_ = new QHBoxLayout;
    el->addLayout(swatches_);
    auto *add = new QPushButton(tr("Add a Colour"));
    auto *remove = new QPushButton(tr("Remove the Last"));
    connect(add, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(Qt::gray, this, tr("Add a Colour"));
        if (c.isValid())
        {
            custom_.push_back(c);
            rebuildCustom();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (custom_.size() > 1)
        {
            custom_.pop_back();
            rebuildCustom();
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
    rebuildCustom();
    showChosen();
}

QString PaletteDialog::chosen() const
{
    const QListWidgetItem *item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QStringLiteral("studio");
}

void PaletteDialog::showChosen()
{
    const QString id = chosen();
    for (const Palette &p : palettes())
    {
        if (p.id == id)
        {
            Palette shown = p;
            if (id == QLatin1String("custom"))
            {
                shown.colours = custom_;
            }
            source_->setText(tr("%1 -- %2").arg(p.name, p.source));
            preview_->setPixmap(lines(shown, 5));
        }
    }
    editor_->setVisible(id == QLatin1String("custom"));
}

void PaletteDialog::rebuildCustom()
{
    while (QLayoutItem *item = swatches_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    for (std::size_t i = 0; i < custom_.size(); ++i)
    {
        auto *b = new QToolButton;
        b->setFixedSize(34, 26);
        b->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;").arg(custom_[i].name()));
        b->setToolTip(custom_[i].name());
        connect(b, &QToolButton::clicked, this, [this, i]() {
            const QColor c = QColorDialog::getColor(custom_[i], this, tr("Colour %1").arg(i + 1));
            if (c.isValid())
            {
                custom_[i] = c;
                rebuildCustom();
            }
        });
        swatches_->addWidget(b);
    }
    swatches_->addStretch(1);
    showChosen();
}

} // namespace studio
