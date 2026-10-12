#pragma once

#include <QFont>
#include <QString>

namespace studio
{

/// The fonts the user chose, kept in QSettings beside the palette.
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
    bool systemInterface = true;   ///< the platform's default rather than a chosen one
    QString figureFamily;       ///< empty: the interface's family
    double figureSize = 9.0;    ///< points, for exported figures' text
};

/// The platform's own font, as the application started with it.
QFont systemFont();

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
