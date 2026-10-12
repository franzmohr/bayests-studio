#include "views/FontDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace studio
{

FontDialog::FontDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Font"));
    resize(560, 480);
    const FontChoice now = currentFonts();
    auto *layout = new QVBoxLayout(this);

    auto *ui = new QGroupBox(tr("Interface"));
    auto *uf = new QFormLayout(ui);
    system_ = new QCheckBox(tr("The studio's font (%1, %2 pt)")
                                .arg(systemFont().family())
                                .arg(systemFont().pointSizeF()));
    system_->setChecked(now.systemInterface);
    system_->setToolTip(tr("Inter, which the studio ships so that it looks the same on every system and screen; the guidance is set in Source Serif 4 beside it."));
    uf->addRow(system_);
    interface_ = new QFontComboBox;
    interface_->setCurrentFont(now.interface);
    interface_->setToolTip(tr("The family of every menu, form and table."));
    uf->addRow(tr("Family"), interface_);
    interfaceSize_ = new QDoubleSpinBox;
    interfaceSize_->setRange(7.0, 18.0);
    interfaceSize_->setSingleStep(0.5);
    interfaceSize_->setDecimals(1);
    interfaceSize_->setSuffix(tr(" pt"));
    interfaceSize_->setValue(now.interface.pointSizeF() > 0 ? now.interface.pointSizeF() : 10.5);
    interfaceSize_->setToolTip(tr("The interface's size in points; plots scale their axis labels with it."));
    uf->addRow(tr("Size"), interfaceSize_);
    interfacePreview_ = new QLabel(tr("Posterior simulation -- 20,000 draws after a burn-in of 2,000"));
    interfacePreview_->setWordWrap(true);
    uf->addRow(interfacePreview_);
    layout->addWidget(ui);

    auto *fig = new QGroupBox(tr("Plots and exported figures"));
    auto *ff = new QFormLayout(fig);
    sameFigure_ = new QCheckBox(tr("The interface's family"));
    sameFigure_->setChecked(now.figureFamily.isEmpty());
    sameFigure_->setToolTip(tr("Plots and exported figures in the interface's family."));
    ff->addRow(sameFigure_);
    figure_ = new QFontComboBox;
    figure_->setCurrentFont(QFont(now.figureFamily.isEmpty() ? now.interface.family() : now.figureFamily));
    figure_->setToolTip(tr("The family of the plots' text, on screen and in every exported figure."));
    ff->addRow(tr("Family"), figure_);
    figureSize_ = new QDoubleSpinBox;
    figureSize_->setRange(6.0, 14.0);
    figureSize_->setSingleStep(0.5);
    figureSize_->setDecimals(1);
    figureSize_->setSuffix(tr(" pt"));
    figureSize_->setValue(now.figureSize);
    figureSize_->setToolTip(tr("The size of an exported figure's text. A figure exported 16 cm wide and placed "
                               "at that width in a document keeps it; axis labels never fall below 8 pt."));
    ff->addRow(tr("Size in exported figures"), figureSize_);
    auto *note = new QLabel(tr("To match a LaTeX document, choose its typeface here -- Latin Modern, TeX Gyre "
                               "Termes or Pagella, or whatever the document sets -- once it is installed."));
    note->setObjectName(QStringLiteral("subtle"));
    note->setWordWrap(true);
    ff->addRow(note);
    figurePreview_ = new QLabel(tr("Response of unemployment to an uncertainty shock, 68% band"));
    figurePreview_->setWordWrap(true);
    ff->addRow(figurePreview_);
    layout->addWidget(fig);
    layout->addStretch(1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);

    connect(system_, &QCheckBox::toggled, this, &FontDialog::update);
    connect(sameFigure_, &QCheckBox::toggled, this, &FontDialog::update);
    connect(interface_, &QFontComboBox::currentFontChanged, this, &FontDialog::update);
    connect(figure_, &QFontComboBox::currentFontChanged, this, &FontDialog::update);
    connect(interfaceSize_, &QDoubleSpinBox::valueChanged, this, &FontDialog::update);
    connect(figureSize_, &QDoubleSpinBox::valueChanged, this, &FontDialog::update);
    update();
}

FontChoice FontDialog::chosen() const
{
    FontChoice c;
    c.systemInterface = system_->isChecked();
    c.interface = c.systemInterface ? systemFont() : interface_->currentFont();
    if (!c.systemInterface)
    {
        c.interface.setPointSizeF(interfaceSize_->value());
    }
    c.figureFamily = sameFigure_->isChecked() ? QString() : figure_->currentFont().family();
    c.figureSize = figureSize_->value();
    return c;
}

void FontDialog::update()
{
    interface_->setEnabled(!system_->isChecked());
    interfaceSize_->setEnabled(!system_->isChecked());
    figure_->setEnabled(!sameFigure_->isChecked());
    const FontChoice c = chosen();
    interfacePreview_->setFont(c.interface);
    QFont f = c.interface;
    if (!c.figureFamily.isEmpty())
    {
        f.setFamily(c.figureFamily);
    }
    f.setPointSizeF(c.figureSize);
    figurePreview_->setFont(f);
    // Room for two lines of whichever font is shown.
    for (QLabel *l : {interfacePreview_, figurePreview_})
    {
        l->setMinimumHeight(QFontMetrics(l->font()).lineSpacing() * 2 + 6);
    }
}

} // namespace studio
