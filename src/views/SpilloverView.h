#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "engine/Spillover.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;
class QwtPlot;

namespace studio
{

/// The connectedness measures of Diebold and Yilmaz of the Application stage:
/// the table with its from and to margins and the total index, the
/// directional measures with their credible intervals, the net pairwise
/// spillovers and -- for a model whose coefficients or covariance move -- the
/// total index over the sample. The numbers are bvartools' spillover();
/// test/structural_test.cpp holds the two together draw by draw.
class SpilloverView : public QWidget
{
    Q_OBJECT

public:
    explicit SpilloverView(QWidget *parent = nullptr);

    void setModel(const QString &file);

private:
    struct Result
    {
        QString error;
        Spillovers measures;
        SpilloverPath path;
        QString pathError;
    };

    SpilloverRequest request() const;
    void schedule();
    void compute();
    void show(const Result &result);
    void showDirectional();

    QString file_;
    SpilloverSupport support_;
    Result last_;
    QFutureWatcher<Result> *watcher_;
    QTimer *debounce_;
    bool pending_ = false;

    QLabel *status_;
    QComboBox *type_;
    QSpinBox *horizon_;
    QSpinBox *period_;
    QLabel *periodLabel_;
    QTabWidget *tabs_;
    QTableWidget *table_;
    QComboBox *measure_;
    QwtPlot *bars_;
    QTableWidget *pairwise_;
    QLabel *pathNote_;
    QwtPlot *path_;
};

} // namespace studio
