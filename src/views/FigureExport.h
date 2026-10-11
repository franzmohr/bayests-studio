#pragma once

#include <QString>
#include <QStringList>
#include <QToolButton>

#include <functional>
#include <vector>

class QPainter;
class QRectF;
class QwtPlot;

namespace studio
{

/// One panel of a figure: a plot, or -- where a grid has a fixed value in
/// place of a plot, as A_0's ones and zeros in the summary plot -- a text.
struct FigureCell
{
    QwtPlot *plot = nullptr;
    QString text;
};

/// A figure as a view would export it: a title, a grid of panels row by row,
/// and the labels of its rows and columns where the view shows them beside
/// the plots rather than in them.
struct FigureSpec
{
    QString title;
    QString subtitle;
    int columns = 1;
    std::vector<FigureCell> cells;
    QStringList columnLabels;
    QStringList rowLabels;
    QString name;              ///< suggested file name, without extension
    double heightMm = 0.0;     ///< 0: from the number of rows
};

using FigureProvider = std::function<FigureSpec()>;

/// The size a spec's figure is written at by default: 16 cm wide, the text
/// width of a LaTeX article, and a height from its rows.
void defaultFigureSize(const FigureSpec &spec, double &widthMm, double &heightMm);

/// Draws the figure into `rect` of `painter`, whose device has 96 logical dots
/// per inch: every plot in the print palette on a white page, whatever the
/// application's theme.
void renderFigure(QPainter &painter, const QRectF &rect, const FigureSpec &spec);

/// Writes the figure, the format from the extension: .pdf (vector, for
/// \includegraphics), .svg, or .png at `dpi`. Returns false with `error` set.
bool writeFigure(const FigureSpec &spec, const QString &path, double widthMm, double heightMm, int dpi,
                 QString *error = nullptr);

/// A context menu on `plot`: Copy Figure, Save Figure As…, and for a figure
/// of several panels Save This Panel As….
void attachFigureExport(QwtPlot *plot, const FigureProvider &provider);

/// An "Export Figure" button with the same menu.
QToolButton *figureExportButton(const FigureProvider &provider);

/// The figures behind every Export Figure button under `root`, as they
/// stand: what a script or a batch export writes without the dialogs.
std::vector<FigureSpec> figureSources(const QWidget *root);

/// The figure of a single plot, titled.
FigureSpec singleFigure(QwtPlot *plot, const QString &title, const QString &name);

} // namespace studio
