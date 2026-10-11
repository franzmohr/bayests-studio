#pragma once

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>

#include "engine/Structural.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace studio
{

/// Identifies a model by sign restrictions: a table of restrictions on the
/// responses to each shock, a budget of tries per draw, and the search run on
/// a worker thread. What it writes is bvartools' layout, so R reads the
/// identification back with the model.
class SignDialog : public QDialog
{
    Q_OBJECT

public:
    SignDialog(const QString &file, const StructuralSupport &support, QWidget *parent = nullptr);

    /// Closing while the search runs cancels it and waits, so the worker never
    /// outlives the dialog it reports to.
    void reject() override;

private:
    struct Outcome
    {
        QString error;
        SignIdentification identification;
    };

    void addRow(const SignRestriction &r);
    SignIdentification readTable() const;
    void identify();

    QString file_;
    StructuralSupport support_;
    QTableWidget *table_;
    QSpinBox *maxTries_;
    QSpinBox *period_;
    QProgressBar *progress_;
    QLabel *status_;
    QPushButton *identify_;
    QPushButton *cancel_;
    QFutureWatcher<Outcome> *watcher_;
    std::atomic<bool> cancelled_{false};
};

} // namespace studio
