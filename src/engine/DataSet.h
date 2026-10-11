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
    arma::mat values;        ///< periods x series

    bool empty() const { return values.n_elem == 0; }
    int series() const { return static_cast<int>(values.n_cols); }
    int length() const { return static_cast<int>(values.n_rows); }
    QString periodLabel(int row) const
    {
        return row >= 0 && row < periods.size() ? periods[row] : QString::number(row + 1);
    }
};

/// Reads a delimited text file: a header row of names, then one row per
/// period. The delimiter (comma, semicolon or tab) is detected from the header.
/// A first column that is not numeric in every row is taken as period labels.
/// Throws std::runtime_error with the line at fault on anything else: a
/// missing value, a ragged row, a non-numeric cell.
DataSet readDelimited(const QString &path);

} // namespace studio
