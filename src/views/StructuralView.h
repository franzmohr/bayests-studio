#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <vector>

#include "engine/Structural.h"

class QCheckBox;
class QPushButton;
class QComboBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QTabWidget;
class QTimer;
class QwtPlot;

namespace studio
{

/// The structural analysis of the Application stage: impulse responses of
/// every variable to one shock, and the forecast error variance decomposition
/// of one variable, from the posterior draws in the file. The numbers are
/// bvartools' -- test/structural_test.cpp holds the two together draw by draw.
///
/// Recomputed a moment after any control changes, on a worker thread.
class StructuralView : public QWidget
{
    Q_OBJECT

public:
    explicit StructuralView(QWidget *parent = nullptr);

    /// The model to analyse; an empty file, or one without draws, disables
    /// the view with the reason.
    void setModel(const QString &file);

signals:
    /// A sign and zero identification wrote a new model file.
    void modelCreated(const QString &file);

private:
    struct Result
    {
        QString error;
        std::vector<arma::mat> responses;   // per draw: horizons x responses
        std::vector<arma::mat> shares;      // per draw: horizons x shocks
    };

    ResponseRequest request() const;
    void schedule();
    void compute();
    void show(const Result &result);
    void rebuildGrid(int k);

    QString file_;
    StructuralSupport support_;
    QFutureWatcher<Result> *watcher_;
    QTimer *debounce_;
    bool pending_ = false;

    QLabel *status_;
    QComboBox *type_;
    QComboBox *impulse_;
    QComboBox *response_;
    QSpinBox *horizon_;
    QSpinBox *period_;
    QLabel *periodLabel_;
    QCheckBox *cumulative_;
    QCheckBox *standardDeviation_;
    QPushButton *signButton_;
    QTabWidget *tabs_;
    QWidget *gridHost_;
    QGridLayout *grid_;
    std::vector<QwtPlot *> plots_;
    QwtPlot *fevd_;
    QLabel *fevdNote_;
};

} // namespace studio
