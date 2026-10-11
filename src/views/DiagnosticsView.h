#pragma once

#include <QWidget>

#include "engine/ModelFileTree.h"

class QComboBox;
class QLabel;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotHistogram;

namespace studio
{

/// Trace and marginal density of one row of a draws dataset, with summary
/// statistics -- the first look at whether a chain mixed.
class DiagnosticsView : public QWidget
{
    Q_OBJECT

public:
    explicit DiagnosticsView(QWidget *parent = nullptr);

    /// Shows the dataset at `path` of `file`; rows are quantities, columns
    /// draws, as BayesTS writes them.
    void showDataset(const QString &file, const QString &path, const QStringList &rowNames = {});
    void clear();

private:
    void showRow(int row);

    Matrix matrix_;
    QString path_;
    QLabel *title_;
    QComboBox *row_;
    QLabel *stats_;
    QwtPlot *trace_;
    QwtPlot *density_;
    QwtPlotCurve *traceCurve_;
    QwtPlotCurve *meanCurve_;
    QwtPlotHistogram *histogram_;
};

} // namespace studio
