// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QComboBox>

namespace studio
{

/// The period of the sample a time-varying model's system is read at, by its
/// date -- "1982 Q4" -- rather than its number. value() and setValue() count
/// periods from one, as the analyses' requests do.
class PeriodBox : public QComboBox
{
    Q_OBJECT

public:
    explicit PeriodBox(QWidget *parent = nullptr);

    /// The model's `count` sample periods, labelled from its file; the last
    /// selected, where a forecast starts.
    void setPeriods(int count, const QString &file);

    int value() const { return currentIndex() + 1; }
    void setValue(int period) { setCurrentIndex(period - 1); }

signals:
    void valueChanged(int period);
};

} // namespace studio
