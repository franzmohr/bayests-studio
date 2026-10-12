// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QColor>
#include <QDialog>

#include <vector>

#include "views/Palettes.h"

class QHBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

namespace studio
{

/// Chooses the palette the plots draw their series from: the studio's own,
/// the five most used of ggplot2, of matplotlib and of the viridis family,
/// an extension's, or one of the user's own -- "My organisation's colours",
/// as many as there are house styles to follow -- which the dialog creates,
/// renames, deletes and edits.
class PaletteDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PaletteDialog(QWidget *parent = nullptr);

    QString chosen() const;
    /// The user's own palettes as the dialog left them.
    std::vector<OwnPalette> own() const { return own_; }

private:
    void rebuildList(const QString &select);
    void showChosen();
    void rebuildSwatches();
    /// The own palette selected, or -1.
    int ownIndex() const;
    void addOwn();
    void renameOwn();
    void deleteOwn();
    QString askName(const QString &title, const QString &current);

    QListWidget *list_;
    QLabel *source_;
    QLabel *preview_;
    QPushButton *rename_;
    QPushButton *delete_;
    QWidget *editor_;
    QHBoxLayout *swatches_;
    std::vector<OwnPalette> own_;
};

} // namespace studio
