#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <vector>

#include "engine/HistoricalDecomposition.h"
#include "views/FigureExport.h"

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QScrollArea;
class QTimer;
class QwtPlot;

namespace studio
{

/// The historical decomposition of the Application stage: the contribution
/// of each identified shock to one variable over the sample, stacked as bars
/// -- the positive parts above zero, the negative below, as bvartools'
/// plot.bvarhd() draws them -- with their sum as a line, and optionally the
/// baseline the shocks leave. For every variable at once it is a matrix laid
/// out as the responses': a column per shock (and the baseline), a row per
/// variable, each panel the contribution with its band. The numbers are
/// bvartools' historical_decomposition(); test/structural_test.cpp holds the
/// two together period by period.
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
        std::vector<int> variables;                 ///< the variables decomposed
        std::vector<HistoricalDecomposition> hd;    ///< one per variable
    };

    void schedule();
    void compute();
    void show();
    /// Every variable at once, in the matrix.
    bool allVariables() const;
    void rebuildGrid();

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
    FigureProvider figure_;
    QScrollArea *scroll_;
    QGridLayout *grid_;
    std::vector<QwtPlot *> plots_;      ///< variable i, part j at i * columns + j
    std::vector<QWidget *> headers_;
    int columns_ = 0;                   ///< the shocks, and the baseline when shown
    bool matrix_ = false;
};

} // namespace studio
