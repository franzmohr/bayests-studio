#pragma once

#include <QDialog>

#include "engine/Examples.h"
#include "engine/HorseRace.h"
#include "engine/VarBuilder.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace studio
{

/// The thesis assistant's baseline: the recommended specification in a few
/// choices -- the variables in their order, VAR or VEC, the lags, the prior,
/// the draws and the seed -- prefilled from a worked example where one is
/// followed. The full builder remains for everything else.
class ThesisBaselineDialog : public QDialog
{
    Q_OBJECT

public:
    ThesisBaselineDialog(const DataSet &data, const WorkflowExample *example, QWidget *parent = nullptr);
    VarBuilderSpec spec() const;

private:
    void update();

    DataSet data_;
    QListWidget *variables_;
    QRadioButton *var_;
    QRadioButton *vec_;
    QSpinBox *rank_;
    QSpinBox *p_;
    QComboBox *prior_;
    QDoubleSpinBox *kappa1_;
    QSpinBox *iterations_;
    QSpinBox *burnin_;
    QLineEdit *seed_;
    QLabel *summary_;
    QPushButton *ok_;
    const WorkflowExample *example_;
};

/// The horse race assistant's design: the variables, where the windows
/// start, the forecast origins, the horizon, the field and its benchmark,
/// and the draws -- with the number of runs it makes.
class HorseRaceDialog : public QDialog
{
    Q_OBJECT

public:
    HorseRaceDialog(const DataSet &data, const WorkflowExample *example, QWidget *parent = nullptr);
    HorseRaceDesign design() const;

private:
    void rebuildCandidates();
    void update();

    DataSet data_;
    QListWidget *variables_;
    QComboBox *first_;
    QComboBox *firstOrigin_;
    QComboBox *lastOrigin_;
    QSpinBox *step_;
    QSpinBox *horizon_;
    QSpinBox *lags_;
    QCheckBox *levels_;
    QComboBox *break_;
    QListWidget *candidates_;
    QComboBox *benchmark_;
    QSpinBox *iterations_;
    QSpinBox *burnin_;
    QLabel *summary_;
    QPushButton *ok_;
    std::vector<HorseRaceCandidate> field_;
};

} // namespace studio
