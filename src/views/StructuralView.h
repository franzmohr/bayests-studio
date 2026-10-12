#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <vector>

#include "engine/Structural.h"
#include "views/FigureExport.h"

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
/// every variable to every shock -- a matrix laid out as the covariance
/// matrix, a column per shock and a row per response -- or to one shock, and
/// the forecast error variance decomposition of every variable -- the same
/// matrix, a row per variable decomposed -- or of one, from the posterior draws in the file. The numbers are
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
        std::vector<int> shocks;            // the shocks computed
        std::vector<std::vector<arma::mat>> responses;   // per shock, per draw: horizons x responses
        std::vector<int> decomposed;        // the variables decomposed
        std::vector<std::vector<arma::mat>> shares;   // per variable, per draw: horizons x shocks
    };

    ResponseRequest request() const;
    void schedule();
    void compute();
    void show(const Result &result);
    void rebuildGrid(int k);
    /// Every shock at once, in the matrix, rather than the one chosen.
    bool allShocks() const;
    void rebuildFevdGrid(int k);
    /// Every variable's decomposition at once, in the matrix.
    bool allResponses() const;

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
    std::vector<QwtPlot *> plots_;   // row by row: response i, shock j at i * k + j in the matrix
    std::vector<QWidget *> headers_;
    bool matrix_ = false;            // what the grid was built for
    QwtPlot *fevd_;
    QLabel *fevdNote_;
    QWidget *fevdScroll_;
    QWidget *fevdGridHost_;
    QGridLayout *fevdGrid_;
    std::vector<QwtPlot *> fevdPlots_;   // variable i, shock j at i * k + j; one variable: shock j at j
    int fevdColumns_ = 1;
    std::vector<QWidget *> fevdHeaders_; // the k shock headers first
    bool fevdMatrix_ = false;
    int columns_ = 1;
    FigureProvider figure_;
};

} // namespace studio
