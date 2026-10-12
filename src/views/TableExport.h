// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QToolButton>

#include <vector>

#include "engine/TableFormat.h"

class QTableWidget;
class QWidget;

namespace studio
{

/// Item data role a view sets on a cell it greys out -- an element variable
/// selection did not reach -- so an export keeps the distinction.
constexpr int TableMutedRole = 0x0100 + 100;   // Qt::UserRole + 100

/// What `table` shows: its visible columns' headers, its row headers where
/// the vertical header is visible, the cells' text, and which are bold or
/// muted. The caption, notes and rules come from setTableExportInfo().
TableContent tableContent(const QTableWidget *table);

/// The caption and notes an export of `table` carries, and the rows a rule is
/// drawn above (a margin row, such as a spillover table's "To others").
/// Called again whenever what the table shows changes.
void setTableExportInfo(QTableWidget *table, const QString &caption, const QString &notes = {},
                        const std::vector<int> &rulesBefore = {});

/// Makes `table` exportable: a context menu and Ctrl+C. Copy puts the table
/// on the clipboard as tab separated text and as HTML, so a spreadsheet or a
/// word processor pastes it as a table; Copy as LaTeX a booktabs table;
/// Save writes CSV, LaTeX or Markdown.
void attachTableExport(QTableWidget *table);

/// A small "Export" button with the same menu, to put beside the table.
QToolButton *tableExportButton(QTableWidget *table);

/// Saves several tables to one file the user picks -- LaTeX one table
/// environment after the other, CSV one block per table with its caption,
/// Markdown one after the other.
void saveTables(QWidget *parent, const std::vector<TableContent> &tables, const QString &suggestedName);

} // namespace studio
