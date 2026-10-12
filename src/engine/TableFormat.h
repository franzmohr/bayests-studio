// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace studio
{

/// A results table as the views show it, independent of any widget, so it
/// can be written out as CSV, LaTeX, Markdown or HTML. Cells are the text
/// the view displays; `bold` and `muted` carry the emphasis it gives them.
struct TableContent
{
    QString caption;
    QString notes;                     ///< printed under the table
    QStringList columnHeaders;
    QStringList rowHeaders;            ///< empty: the table has none
    std::vector<QStringList> cells;    ///< rows of columnHeaders.size() cells
    std::vector<std::vector<bool>> bold;
    std::vector<std::vector<bool>> muted;
    std::vector<int> rulesBefore;      ///< rows (0-based) a rule is drawn above, as a margin row
};

/// RFC 4180: comma separated, a field quoted where it holds the separator, a
/// quote or a line break. The header row starts with an empty field where
/// the table has row headers.
QString toCsv(const TableContent &table, QChar separator = QLatin1Char(','));

/// A booktabs table environment with caption and label: numeric columns set
/// right, bold and muted (\textcolor{gray}) emphasis kept, LaTeX's special
/// characters escaped, and the Greek letters, arrows, minus signs and stars
/// the views print turned into math. The first line says which packages it
/// needs.
QString toLatex(const TableContent &table, const QString &label = {});

/// A pipe table, bold kept.
QString toMarkdown(const TableContent &table);

/// An HTML table, for the clipboard: a spreadsheet or word processor pastes
/// it as a table.
QString toHtml(const TableContent &table);

/// One cell's text as LaTeX.
QString latexText(const QString &text);

} // namespace studio
