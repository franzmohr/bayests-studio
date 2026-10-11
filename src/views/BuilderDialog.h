#pragma once

#include <QDialog>
#include <QTemporaryDir>

#include "engine/VarBuilder.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTimer;

namespace studio
{

/// The model builder: a model file from a data set, specified with tick boxes.
///
/// Every change is written to a scratch file and handed to `bayests check`
/// (ModelRunner::check) a moment later, so what the panel on the right says is
/// what BayesTS itself says about the file -- the form disables the
/// combinations BayesTS refuses, but the check is the authority.
class BuilderDialog : public QDialog
{
    Q_OBJECT

public:
    BuilderDialog(const DataSet &data, const QString &projectDir, QWidget *parent = nullptr);

    /// The file written by Create, once the dialog was accepted.
    QString createdFile() const { return created_; }

    /// Writes the model as specified to `path` and accepts the dialog; what
    /// Create does after asking where. False, with a message shown, when the
    /// form is refused.
    bool createAt(const QString &path);

private:
    QWidget *buildDataBox();
    QWidget *buildSpecBox();
    QWidget *buildPriorBox();
    QWidget *buildSamplerBox();
    QWidget *buildSummary();

    VarBuilderSpec readForm() const;
    void updateEnabled();
    void scheduleCheck();
    void runCheck();
    void create();
    QString suggestedName() const;

    DataSet data_;
    QString projectDir_;
    QString created_;
    QTemporaryDir scratch_;
    QTimer *debounce_;

    // Data
    QListWidget *variables_;
    QComboBox *first_;
    QComboBox *last_;

    // Specification
    QRadioButton *familyVar_;
    QRadioButton *familyVec_;
    QSpinBox *rank_;
    QCheckBox *restrictConstant_;
    QCheckBox *restrictTrend_;
    QSpinBox *p_;
    QCheckBox *constant_;
    QCheckBox *trend_;
    QRadioButton *coefConstant_;
    QRadioButton *coefTvp_;
    QCheckBox *nonCentred_;
    QRadioButton *errWishart_;
    QRadioButton *errGamma_;
    QRadioButton *errSv_;
    QCheckBox *covar_;
    QCheckBox *structural_;
    QComboBox *selection_;
    QDoubleSpinBox *inprior_;
    QCheckBox *selectDet_;
    QCheckBox *selectCovar_;

    // Priors
    QCheckBox *minnesota_;
    QDoubleSpinBox *coefPrecision_;
    QDoubleSpinBox *detPrecision_;
    QDoubleSpinBox *kappa1_;
    QDoubleSpinBox *kappa2_;
    QDoubleSpinBox *kappa4_;
    QGroupBox *cointBox_;
    QDoubleSpinBox *cointShrinkage_;
    QDoubleSpinBox *cointPTauInv_;
    QDoubleSpinBox *cointRho_;
    QGroupBox *stateBox_;
    QDoubleSpinBox *stateShape_;
    QLineEdit *stateRate_;
    QLineEdit *stateRateDet_;
    QLineEdit *omegaV_;
    QGroupBox *wishartBox_;
    QSpinBox *dfExtra_;
    QDoubleSpinBox *wishartScale_;
    QGroupBox *gammaBox_;
    QDoubleSpinBox *gammaShape_;
    QLineEdit *gammaRate_;
    QGroupBox *svBox_;
    QDoubleSpinBox *svShape_;
    QLineEdit *svRate_;
    QLineEdit *svState_;

    // Sampler and forecast
    QSpinBox *iterations_;
    QSpinBox *burnin_;
    QSpinBox *thin_;
    QLineEdit *seed_;
    QSpinBox *horizon_;
    QSpinBox *holdout_;

    // Summary
    QLabel *algorithm_;
    QLabel *dimensions_;
    QLabel *status_;
    QLabel *notes_;
    QPushButton *createButton_;
};

} // namespace studio
