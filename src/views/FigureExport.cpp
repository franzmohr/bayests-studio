#include "views/FigureExport.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSpinBox>
#include <QSvgGenerator>

#include <qwt_plot.h>
#include <qwt_plot_renderer.h>

#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{

constexpr double LogicalDpi = 96.0;

double px(double mm)
{
    return mm / 25.4 * LogicalDpi;
}

/// The print palette: black on white, whatever the application's theme.
QPalette printPalette()
{
    QPalette p;
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
    {
        p.setColor(group, QPalette::Window, Qt::white);
        p.setColor(group, QPalette::Base, Qt::white);
        p.setColor(group, QPalette::WindowText, Qt::black);
        p.setColor(group, QPalette::Text, Qt::black);
        p.setColor(group, QPalette::ButtonText, Qt::black);
        p.setColor(group, QPalette::Mid, QColor(110, 110, 110));
        p.setColor(group, QPalette::Dark, QColor(80, 80, 80));
        p.setColor(group, QPalette::Light, QColor(210, 210, 210));
    }
    return p;
}

QString fileStem(const QString &name)
{
    QString s;
    for (const QChar c : name.toLower())
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
    return s.isEmpty() ? QStringLiteral("figure") : s.left(60);
}

/// Asks for the size and, for PNG, the resolution.
bool askSize(QWidget *parent, bool png, double &widthMm, double &heightMm, int &dpi)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Figure Size"));
    auto *form = new QFormLayout(&dialog);
    auto *width = new QDoubleSpinBox;
    width->setRange(2.0, 100.0);
    width->setSuffix(QObject::tr(" cm"));
    width->setValue(widthMm / 10.0);
    auto *height = new QDoubleSpinBox;
    height->setRange(2.0, 100.0);
    height->setSuffix(QObject::tr(" cm"));
    height->setValue(heightMm / 10.0);
    auto *resolution = new QSpinBox;
    resolution->setRange(72, 1200);
    resolution->setSuffix(QObject::tr(" dpi"));
    resolution->setValue(dpi);
    form->addRow(QObject::tr("Width"), width);
    form->addRow(QObject::tr("Height"), height);
    if (png)
    {
        form->addRow(QObject::tr("Resolution"), resolution);
    }
    auto *hint = new QLabel(QObject::tr("Text is set at 9 pt at this size: include the figure at the same width, "
                                        "\\includegraphics[width=%1cm]{...}, and it matches a 10 pt document.")
                                .arg(QStringLiteral("…")));
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("subtle"));
    form->addRow(hint);
    QObject::connect(width, &QDoubleSpinBox::valueChanged, hint, [hint](double w) {
        hint->setText(QObject::tr("Text is set at 9 pt at this size: include the figure at the same width, "
                                  "\\includegraphics[width=%1cm]{...}, and it matches a 10 pt document.")
                          .arg(w, 0, 'g', 3));
    });
    emit width->valueChanged(width->value());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }
    widthMm = width->value() * 10.0;
    heightMm = height->value() * 10.0;
    dpi = resolution->value();
    return true;
}

void saveFigure(QWidget *parent, const FigureSpec &spec)
{
    if (spec.cells.empty())
    {
        return;
    }
    QString filter;
    const QString pdf = QObject::tr("PDF, vector (*.pdf)");
    const QString svg = QObject::tr("SVG, vector (*.svg)");
    const QString png = QObject::tr("PNG (*.png)");
    QString path = QFileDialog::getSaveFileName(parent, QObject::tr("Export Figure"),
                                                QDir::current().filePath(fileStem(spec.name) + QStringLiteral(".pdf")),
                                                QStringList{pdf, svg, png}.join(QStringLiteral(";;")), &filter);
    if (path.isEmpty())
    {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty())
    {
        path += filter == svg ? QStringLiteral(".svg") : filter == png ? QStringLiteral(".png") : QStringLiteral(".pdf");
    }
    double widthMm = 0.0, heightMm = 0.0;
    defaultFigureSize(spec, widthMm, heightMm);
    int dpi = 300;
    if (!askSize(parent, QFileInfo(path).suffix().toLower() == QLatin1String("png"), widthMm, heightMm, dpi))
    {
        return;
    }
    QString error;
    if (!writeFigure(spec, path, widthMm, heightMm, dpi, &error))
    {
        QMessageBox::warning(parent, QObject::tr("Export Figure"), error);
    }
}

void copyFigure(const FigureSpec &spec)
{
    double widthMm = 0.0, heightMm = 0.0;
    defaultFigureSize(spec, widthMm, heightMm);
    const double ratio = 2.0;   // 192 dpi: sharp when pasted into a document
    QImage image(static_cast<int>(std::ceil(px(widthMm) * ratio)), static_cast<int>(std::ceil(px(heightMm) * ratio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        renderFigure(painter, QRectF(0, 0, px(widthMm), px(heightMm)), spec);
    }
    QApplication::clipboard()->setImage(image);
}

void fillMenu(QMenu *menu, QWidget *parent, const FigureProvider &provider, QwtPlot *panel)
{
    menu->addAction(QObject::tr("Copy Figure"), parent, [provider]() { copyFigure(provider()); });
    menu->addAction(QObject::tr("Save Figure As…"), parent, [parent, provider]() { saveFigure(parent->window(), provider()); });
    if (panel != nullptr)
    {
        menu->addAction(QObject::tr("Save This Panel As…"), parent, [parent, provider, panel]() {
            const FigureSpec all = provider();
            if (all.cells.size() > 1)
            {
                FigureSpec one;
                one.name = all.name + QStringLiteral("-panel");
                one.cells.push_back({panel, {}});
                saveFigure(parent->window(), one);
            }
            else
            {
                saveFigure(parent->window(), all);
            }
        });
    }
}

/// Holds a button's provider, so figureSources() can reach it.
class FigureSource : public QObject
{
public:
    FigureSource(FigureProvider provider, QObject *parent) : QObject(parent), provider(std::move(provider))
    {
        setObjectName(QStringLiteral("figureSource"));
    }
    FigureProvider provider;
};

} // namespace

std::vector<FigureSpec> figureSources(const QWidget *root)
{
    std::vector<FigureSpec> out;
    for (const QObject *o : root->findChildren<QObject *>(QStringLiteral("figureSource")))
    {
        if (const auto *source = dynamic_cast<const FigureSource *>(o))
        {
            FigureSpec spec = source->provider();
            if (!spec.cells.empty())
            {
                out.push_back(std::move(spec));
            }
        }
    }
    return out;
}

void defaultFigureSize(const FigureSpec &spec, double &widthMm, double &heightMm)
{
    widthMm = 160.0;
    const int columns = std::max(1, spec.columns);
    const int rows = static_cast<int>((spec.cells.size() + static_cast<std::size_t>(columns) - 1) /
                                      static_cast<std::size_t>(columns));
    if (spec.heightMm > 0.0)
    {
        heightMm = spec.heightMm;
    }
    else if (spec.cells.size() <= 1)
    {
        heightMm = 100.0;
    }
    else
    {
        // Panels about as tall as two thirds of their width, within an A4 page.
        const double panel = std::min(70.0, widthMm / columns * 0.68);
        heightMm = std::min(240.0, rows * panel + (spec.title.isEmpty() ? 0.0 : 10.0) +
                                       (spec.columnLabels.isEmpty() ? 0.0 : 6.0));
    }
}

void renderFigure(QPainter &painter, const QRectF &rect, const FigureSpec &spec)
{
    painter.save();
    painter.fillRect(rect, Qt::white);
    QFont font = QApplication::font();
    font.setPointSizeF(9.0);
    painter.setFont(font);
    painter.setPen(Qt::black);
    QRectF area = rect.adjusted(2, 2, -2, -2);

    // Title and subtitle above the grid.
    if (!spec.title.isEmpty())
    {
        QFont bold = font;
        bold.setBold(true);
        bold.setPointSizeF(10.0);
        painter.setFont(bold);
        const double h = QFontMetricsF(bold).height() + 4;
        painter.drawText(QRectF(area.left(), area.top(), area.width(), h), Qt::AlignCenter, spec.title);
        area.setTop(area.top() + h);
        painter.setFont(font);
    }
    if (!spec.subtitle.isEmpty())
    {
        const double h = QFontMetricsF(font).height() + 2;
        painter.drawText(QRectF(area.left(), area.top(), area.width(), h), Qt::AlignCenter, spec.subtitle);
        area.setTop(area.top() + h + 2);
    }

    const int columns = std::max(1, spec.columns);
    const int rows = static_cast<int>((spec.cells.size() + static_cast<std::size_t>(columns) - 1) /
                                      static_cast<std::size_t>(columns));
    // Row labels to the left, column labels above.
    QFont label = font;
    label.setBold(true);
    const QFontMetricsF lm(label);
    double left = 0.0;
    for (const QString &r : spec.rowLabels)
    {
        left = std::max(left, lm.horizontalAdvance(r) + 8);
    }
    const double top = spec.columnLabels.isEmpty() ? 0.0 : lm.height() + 4;
    const QRectF grid(area.left() + left, area.top() + top, area.width() - left, area.height() - top);
    const double cw = grid.width() / columns;
    const double ch = rows > 0 ? grid.height() / rows : grid.height();
    painter.setFont(label);
    for (int c = 0; c < columns && c < spec.columnLabels.size(); ++c)
    {
        painter.drawText(QRectF(grid.left() + c * cw, area.top(), cw, top), Qt::AlignCenter, spec.columnLabels[c]);
    }
    for (int r = 0; r < rows && r < spec.rowLabels.size(); ++r)
    {
        painter.drawText(QRectF(area.left(), grid.top() + r * ch, left, ch), Qt::AlignVCenter | Qt::AlignLeft,
                         spec.rowLabels[r]);
    }
    painter.setFont(font);

    QwtPlotRenderer renderer;
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardBackground, true);
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardCanvasBackground, true);
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardCanvasFrame, true);
    for (std::size_t i = 0; i < spec.cells.size(); ++i)
    {
        const int r = static_cast<int>(i) / columns;
        const int c = static_cast<int>(i) % columns;
        const QRectF cell(grid.left() + c * cw, grid.top() + r * ch, cw, ch);
        const FigureCell &f = spec.cells[i];
        if (f.plot == nullptr)
        {
            painter.drawText(cell, Qt::AlignCenter, f.text);
            continue;
        }
        // The print palette for the render, and axis labels of at least 8 pt
        // -- the screen's compact panels use less -- the plot's own after.
        const QPalette own = f.plot->palette();
        f.plot->setPalette(printPalette());
        std::vector<std::pair<int, QFont>> fonts;
        for (const int axis : {QwtPlot::xBottom, QwtPlot::yLeft, QwtPlot::yRight, QwtPlot::xTop})
        {
            const QFont font = f.plot->axisFont(axis);
            fonts.emplace_back(axis, font);
            if (font.pointSizeF() > 0 && font.pointSizeF() < 8.0)
            {
                QFont larger = font;
                larger.setPointSizeF(8.0);
                f.plot->setAxisFont(axis, larger);
            }
        }
        f.plot->updateLayout();
        renderer.render(f.plot, &painter, cell.adjusted(2, 2, -2, -2));
        for (const auto &[axis, font] : fonts)
        {
            f.plot->setAxisFont(axis, font);
        }
        f.plot->setPalette(own);
        f.plot->updateLayout();
    }
    painter.restore();
}

bool writeFigure(const FigureSpec &spec, const QString &path, double widthMm, double heightMm, int dpi,
                 QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QRectF page(0, 0, px(widthMm), px(heightMm));
    const auto fail = [&](const QString &why) {
        if (error != nullptr)
        {
            *error = why;
        }
        return false;
    };
    if (suffix == QLatin1String("pdf"))
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QSizeF(widthMm, heightMm), QPageSize::Millimeter));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(static_cast<int>(LogicalDpi));
        writer.setTitle(spec.title.isEmpty() ? spec.name : spec.title);
        writer.setCreator(QStringLiteral("BayesTS Studio"));
        QPainter painter;
        if (!painter.begin(&writer))
        {
            return fail(QObject::tr("Cannot write %1.").arg(path));
        }
        renderFigure(painter, page, spec);
        return true;
    }
    if (suffix == QLatin1String("svg"))
    {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(page.size().toSize());
        generator.setViewBox(page);
        generator.setResolution(static_cast<int>(LogicalDpi));
        generator.setTitle(spec.title.isEmpty() ? spec.name : spec.title);
        generator.setDescription(QStringLiteral("BayesTS Studio"));
        QPainter painter;
        if (!painter.begin(&generator))
        {
            return fail(QObject::tr("Cannot write %1.").arg(path));
        }
        renderFigure(painter, page, spec);
        return true;
    }
    if (suffix == QLatin1String("png"))
    {
        const double ratio = dpi / LogicalDpi;
        QImage image(static_cast<int>(std::ceil(page.width() * ratio)), static_cast<int>(std::ceil(page.height() * ratio)),
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            renderFigure(painter, page, spec);
        }
        image.setDotsPerMeterX(static_cast<int>(std::lround(dpi / 0.0254)));
        image.setDotsPerMeterY(static_cast<int>(std::lround(dpi / 0.0254)));
        return image.save(path, "PNG") ? true : fail(QObject::tr("Cannot write %1.").arg(path));
    }
    return fail(QObject::tr("Choose .pdf, .svg or .png."));
}

void attachFigureExport(QwtPlot *plot, const FigureProvider &provider)
{
    plot->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(plot, &QWidget::customContextMenuRequested, plot, [plot, provider](const QPoint &at) {
        QMenu menu(plot);
        fillMenu(&menu, plot, provider, plot);
        menu.exec(plot->mapToGlobal(at));
    });
}

QToolButton *figureExportButton(const FigureProvider &provider)
{
    auto *button = new QToolButton;
    button->setText(QObject::tr("Export Figure"));
    button->setToolTip(QObject::tr("Copy the figure, or save it as PDF, SVG or PNG"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setObjectName(QStringLiteral("figureExport"));
    auto *menu = new QMenu(button);
    fillMenu(menu, button, provider, nullptr);
    button->setMenu(menu);
    new FigureSource(provider, button);
    return button;
}

FigureSpec singleFigure(QwtPlot *plot, const QString &title, const QString &name)
{
    FigureSpec spec;
    spec.title = title;
    spec.name = name;
    spec.cells.push_back({plot, {}});
    return spec;
}

} // namespace studio
