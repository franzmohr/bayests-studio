#pragma once

#include <QFont>
#include <QString>

namespace studio
{

/// The fonts the user chose, kept in QSettings beside the palette.
///
/// The studio ships its own, so that it looks the same on every system and
/// every screen: Inter (Rasmus Andersson, SIL OFL 1.1) for the interface,
/// the tables and the figures, with tabular figures where numbers stand in
/// columns -- table cells and number fields -- so that they line up,
/// and Source Serif 4 (Adobe, SIL OFL 1.1) for the text read at length -- the
/// assistant's guidance and the examples' notes. Both are in the resources
/// (src/app/fonts) and registered at start-up; their licences are installed
/// beside the others.
///
/// The interface font is every widget's. The figure font is what plots draw
/// their axes and labels in, on screen and in an exported figure -- the
/// interface's family unless the user names another, so a thesis can set its
/// figures in the document's typeface. Sizes in a plot stay relative to the
/// interface's, so a figure family never makes the screen unreadable; an
/// exported figure's text is set at `figureFontSize()` points.
struct FontChoice
{
    QFont interface;            ///< the application font
    bool systemInterface = true;   ///< the studio's own font rather than a chosen one
    QString figureFamily;       ///< empty: the interface's family
    double figureSize = 9.0;    ///< points, for exported figures' text
};

/// Registers the fonts the studio ships. Call once, before anything asks
/// for a font; returns how many families were added (two).
int registerStudioFonts();

/// The studio's interface font: Inter at 10.5 pt with tabular figures, or
/// the platform's own where the resources are missing.
QFont systemFont();

/// The font of text read at length, at the interface's size plus half a
/// point: Source Serif 4, falling back to the interface's family for the
/// symbols it lacks (✓, ✗, arrows).
QFont readingFont();

FontChoice currentFonts();

/// Applies the choice to the application, and keeps it for the next start
/// unless `persist` is false (the smoke modes).
void setFonts(const FontChoice &choice, bool persist = true);

/// The family plots draw in, at the size `pointSize`.
QFont figureFont(double pointSize);

/// The size of an exported figure's text, in points.
double figureFontSize();

/// At start-up: the font kept from last time, if any.
void applySavedFonts();

} // namespace studio
