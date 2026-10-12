// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

#include <vector>

namespace studio
{

/// A colour palette for the series of a plot. Three kinds: a fixed list,
/// cycled (ColorBrewer, matplotlib); a list that depends on how many series
/// there are (ggplot2's hue palette); and a continuous map sampled evenly
/// (the viridis family).
struct Palette
{
    enum class Kind
    {
        Qualitative,
        ByCount,
        Sequential,
    };

    QString id;
    QString name;
    QString family;         ///< "BayesTS Studio", "ggplot2", "matplotlib", "viridis", "Custom"
    QString source;         ///< where the colours come from, and their licence
    Kind kind = Kind::Qualitative;
    std::vector<QColor> colours;                 ///< Qualitative, Sequential
    std::vector<std::vector<QColor>> byCount;    ///< ByCount: row n - 1 for n series

    /// The colour of series `index` of `count`; `count` 0 when it is not
    /// known, which takes six for the palettes that depend on it.
    QColor colour(int index, int count = 0) const;
};

/// The palettes the studio ships, the user's own last.
const std::vector<Palette> &palettes();

/// Builds the list again with the extensions' palettes (engine/Extensions.h)
/// last before the user's own, keeping the current one; returns what was
/// wrong with any that were left out.
QStringList reloadPalettes();

/// The palette plots draw from; set from the settings at start-up.
const Palette &currentPalette();
/// Kept in the settings unless `persist` is false.
void setCurrentPalette(const QString &id, bool persist = true);

/// One of the user's own palettes, listed under "My organisation's colours":
/// a house style, a university's, a journal's.
struct OwnPalette
{
    QString name;
    std::vector<QColor> colours;
};

/// The user's own palettes, kept in the settings. Until the user changes
/// them, one: "My palette", in what an earlier version kept, or Okabe and
/// Ito's colours.
std::vector<OwnPalette> ownPalettes();
/// Keeps them and builds the list again, the current palette kept where it
/// is still there.
void setOwnPalettes(const std::vector<OwnPalette> &own);
/// The heading they are listed under, and the id of one: "own:" + its name.
QString ownPaletteFamily();
QString ownPaletteId(const QString &name);

} // namespace studio
