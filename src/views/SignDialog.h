#pragma once

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>
#include <map>
#include <vector>

#include "engine/SignZero.h"
#include "engine/Structural.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QSpinBox;
class QTabBar;
class QTableWidget;

namespace studio
{

/// Identifies a model by sign restrictions: a grid per horizon of the
/// responses (rows) to each shock (columns), each cell left free or set to
/// +, - or 0, a budget of tries per draw, and the search run on a worker
/// thread. What it writes is bvartools' layout, so R reads the
/// identification back with the model.
class SignDialog : public QDialog
{
    Q_OBJECT

public:
    SignDialog(const QString &file, const StructuralSupport &support, QWidget *parent = nullptr);

    /// Closing while the search runs cancels it and waits, so the worker never
    /// outlives the dialog it reports to.
    void reject() override;

    /// The model file a sign and zero identification wrote, a resample of
    /// the posterior beside the original; empty after a sign identification,
    /// which is written into the model itself.
    QString createdFile() const { return created_; }

    /// Fills the grids with `restrictions` -- a worked example's, for the user
    /// to read and run -- in place of what the dialog started with.
    /// The grid, the threshold (the first restriction's) and the shocks
    /// rotated (0: all) an example names.
    void preset(const std::vector<SignRestriction> &restrictions, int rotated = 0);

private:
    struct Outcome
    {
        QString error;
        SignIdentification identification;
        bool zero = false;
        SignZeroResult signZero;
    };

    /// A cell's sign: +1, -1, 0, or Free.
    static constexpr int Free = 2;

    void setSign(int horizon, int response, int shock, int sign);
    void rebuildTabs(int select);
    void showGrid();
    void paintCell(int response, int shock);
    int currentHorizon() const;
    void addHorizon();
    void removeHorizon();
    bool eventFilter(QObject *watched, QEvent *event) override;
    SignIdentification readTable() const;
    void identify();
    void updateMode();
    bool hasZero() const;

    QString file_;
    StructuralSupport support_;
    QTabBar *horizons_;
    QTableWidget *grid_;
    QLabel *summary_;
    /// Horizon (-1 the long run) to the k x k signs, row-major by response.
    std::map<int, std::vector<int>> signs_;
    QSpinBox *maxTries_;
    QDoubleSpinBox *threshold_;
    QSpinBox *rotated_;
    QSpinBox *period_;
    QGroupBox *arwBox_;
    QSpinBox *arwTries_;
    QCheckBox *smooth_;
    QCheckBox *oneSided_;
    QSpinBox *resample_;
    QLineEdit *output_;
    QLabel *mode_;
    QString created_;
    QProgressBar *progress_;
    QLabel *status_;
    QPushButton *identify_;
    QPushButton *cancel_;
    QFutureWatcher<Outcome> *watcher_;
    std::atomic<bool> cancelled_{false};
};

} // namespace studio
