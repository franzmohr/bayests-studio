#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "engine/HistoricalDecomposition.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class QwtPlot;

namespace studio
{

/// The historical decomposition of the Application stage: the contribution
/// of each identified shock to one variable over the sample, stacked as bars
/// -- the positive parts above zero, the negative below, as bvartools'
/// plot.bvarhd() draws them -- with their sum as a line, and optionally the
/// baseline the shocks leave. The numbers are bvartools'
/// historical_decomposition(); test/structural_test.cpp holds the two
/// together period by period.
class HistoricalDecompositionView : public QWidget
{
    Q_OBJECT

public:
    explicit HistoricalDecompositionView(QWidget *parent = nullptr);

    void setModel(const QString &file);

private:
    struct Result
    {
        QString error;
        HistoricalDecomposition hd;
    };

    void schedule();
    void compute();
    void show();

    QString file_;
    HdSupport support_;
    Result last_;
    QFutureWatcher<Result> *watcher_;
    QTimer *debounce_;
    bool pending_ = false;

    QLabel *status_;
    QComboBox *response_;
    QComboBox *type_;
    QComboBox *statistic_;
    QCheckBox *baseline_;
    QwtPlot *plot_;
};

} // namespace studio
