// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include "bayests/arma.h"

namespace studio
{

/// A table of time series as imported: one row per period, one column per
/// series, with names and optional period labels ("1960 Q2").
struct DataSet
{
    QString source;          ///< the file it came from
    QStringList names;       ///< one per column
    QStringList periods;     ///< one per row; empty when the file had no labels
    arma::mat values;        ///< periods x series; NaN where a value was not observed

    bool empty() const { return values.n_elem == 0; }
    int series() const { return static_cast<int>(values.n_cols); }
    int length() const { return static_cast<int>(values.n_rows); }
    QString periodLabel(int row) const
    {
        return row >= 0 && row < periods.size() ? periods[row] : QString::number(row + 1);
    }
};

/// R's `tsp` of a run of periods: the time of the first and the last, and the
/// number of periods per unit of time -- 4 for "1960 Q2", 12 for "1960-04",
/// 1 for "1960". `known` is false where the labels follow none of these, and
/// the index is then 1, 2, ... at frequency one, which is what R gives a
/// series without one.
struct TimeIndex
{
    double start = 1.0;
    double end = 1.0;
    double frequency = 1.0;
    bool known = false;
};

/// The index of rows `first` to `last` of `data`, from its period labels.
TimeIndex timeIndex(const DataSet &data, int first, int last);

/// Reads a delimited text file: a header row of names, then one row per
/// period. The delimiter (comma, semicolon or tab) is detected from the header.
/// A first column that is not numeric in every row is taken as period labels.
/// Throws std::runtime_error with the line at fault on anything else: a
/// missing value, a ragged row, a non-numeric cell.
DataSet readDelimited(const QString &path);

} // namespace studio
