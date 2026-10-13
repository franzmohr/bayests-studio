// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/PeriodBox.h"

#include "engine/ModelFileTree.h"

#include <algorithm>

namespace studio
{

PeriodBox::PeriodBox(QWidget *parent) : QComboBox(parent)
{
    setMaxVisibleItems(24);
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(this, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
        {
            emit valueChanged(index + 1);
        }
    });
}

void PeriodBox::setPeriods(int count, const QString &file)
{
    const QSignalBlocker block(this);
    clear();
    addItems(samplePeriodLabels(file, std::max(1, count)));
    setCurrentIndex(this->count() - 1);
}

} // namespace studio
