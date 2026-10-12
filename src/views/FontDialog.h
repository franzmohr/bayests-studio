// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QDialog>

#include "views/Fonts.h"

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;

namespace studio
{

/// View > Font: the interface's font, and the one plots and exported figures
/// set their text in.
class FontDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FontDialog(QWidget *parent = nullptr);
    FontChoice chosen() const;

private:
    void update();

    QCheckBox *system_;
    QFontComboBox *interface_;
    QDoubleSpinBox *interfaceSize_;
    QCheckBox *sameFigure_;
    QFontComboBox *figure_;
    QDoubleSpinBox *figureSize_;
    QLabel *interfacePreview_;
    QLabel *figurePreview_;
};

} // namespace studio
