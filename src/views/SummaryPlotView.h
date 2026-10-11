#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <memory>

#include "engine/CoefficientSummary.h"
#include "views/FigureExport.h"

class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;

namespace studio
{

/// bvartools' summary plot, plot.bvarmodel(): one grid per block of
/// coefficients, a row per equation and a column per regressor, each panel a
/// histogram, trace or box plot of the draws -- or, where the coefficient
/// moves, its median and credible band over the sample. A block wider than
/// six columns is split into pages of nearly equal width, as there.
class SummaryPlotView : public QWidget
{
    Q_OBJECT

public:
    explicit SummaryPlotView(QWidget *parent = nullptr);

    void setModel(const QString &file);

private:
    struct Result
    {
        QString error;
        std::shared_ptr<CoefficientSummary> summary;
    };

    void compute();
    void rebuildPages();
    void showPage();

    QString file_;
    std::shared_ptr<CoefficientSummary> summary_;
    QFutureWatcher<Result> *watcher_;
    bool pending_ = false;

    QLabel *status_;
    QComboBox *page_;
    QComboBox *type_;
    QDoubleSpinBox *ci_;
    QLabel *title_;
    QWidget *gridHost_;
    QGridLayout *grid_;
    FigureSpec figure_;   // the page shown
};

} // namespace studio
