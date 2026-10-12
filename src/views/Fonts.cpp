// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/Fonts.h"

#include <QApplication>
#include <QDirIterator>
#include <QFontDatabase>
#include <QSettings>

#include <optional>

namespace studio
{

namespace
{

std::optional<QFont> &startFont()
{
    static std::optional<QFont> font;
    return font;
}

const QString interfaceFamily = QStringLiteral("Inter");
// The size of Claude's and most modern applications' body text: Windows' 9 pt
// is cramped on a 13" laptop, and Inter's tall lowercase reads at 10.5 pt as
// comfortably on a wide monitor.
constexpr double interfaceSize = 10.5;

bool registered(const QString &family)
{
    return QFontDatabase::families().contains(family);
}

/// Numbers in columns: every digit the same width. Only where numbers stand
/// in columns -- table cells and number fields: Inter's tabular set widens the
/// hyphen too, which running text, file names and axis labels must not have
/// ("burn - in", "lags - 1.h5", "- 0.5").
QFont tabular(QFont f)
{
    f.setFeature(QFont::Tag("tnum"), 1);
    return f;
}

void applyToApplication(const QFont &f)
{
    QApplication::setFont(f);
    for (const char *numbers : {"QTableView", "QAbstractSpinBox"})
    {
        QApplication::setFont(tabular(f), numbers);
    }
}

FontChoice &choice()
{
    static FontChoice c = []() {
        FontChoice f;
        const QSettings settings;
        const QString saved = settings.value(QStringLiteral("appearance/font")).toString();
        f.interface = systemFont();
        if (!saved.isEmpty())
        {
            QFont font;
            if (font.fromString(saved))
            {
                f.interface = font;
                f.systemInterface = false;
            }
        }
        f.figureFamily = settings.value(QStringLiteral("plots/fontFamily")).toString();
        f.figureSize = settings.value(QStringLiteral("plots/fontSize"), 9.0).toDouble();
        if (f.figureSize < 5.0 || f.figureSize > 16.0)
        {
            f.figureSize = 9.0;
        }
        return f;
    }();
    return c;
}

} // namespace

int registerStudioFonts()
{
    QStringList families;
    QDirIterator it(QStringLiteral(":/fonts"), {QStringLiteral("*.ttf")});
    while (it.hasNext())
    {
        const int id = QFontDatabase::addApplicationFont(it.next());
        for (const QString &family : QFontDatabase::applicationFontFamilies(id))
        {
            if (!families.contains(family))
            {
                families << family;
            }
        }
    }
    return static_cast<int>(families.size());
}

QFont systemFont()
{
    if (!startFont())
    {
        QFont f = QApplication::font();
        if (registered(interfaceFamily))
        {
            f = QFont(interfaceFamily);
            f.setPointSizeF(interfaceSize);
        }
        startFont() = f;
    }
    return *startFont();
}

QFont readingFont()
{
    QFont f = choice().interface;
    f.setPointSizeF((f.pointSizeF() > 0 ? f.pointSizeF() : interfaceSize) + 0.5);
    return f;
}

FontChoice currentFonts()
{
    return choice();
}

void setFonts(const FontChoice &c, bool persist)
{
    FontChoice &kept = choice();
    kept = c;
    if (kept.systemInterface)
    {
        kept.interface = systemFont();
    }
    applyToApplication(kept.interface);
    if (persist)
    {
        QSettings settings;
        if (kept.systemInterface)
        {
            settings.remove(QStringLiteral("appearance/font"));
        }
        else
        {
            settings.setValue(QStringLiteral("appearance/font"), kept.interface.toString());
        }
        settings.setValue(QStringLiteral("plots/fontFamily"), kept.figureFamily);
        settings.setValue(QStringLiteral("plots/fontSize"), kept.figureSize);
    }
}

QFont figureFont(double pointSize)
{
    const FontChoice &c = choice();
    QFont f = c.interface;
    if (!c.figureFamily.isEmpty())
    {
        f.setFamily(c.figureFamily);
    }
    f.setPointSizeF(pointSize);
    return f;
}

double figureFontSize()
{
    return choice().figureSize;
}

void applySavedFonts()
{
    systemFont();   // the studio's, before anything replaces it
    applyToApplication(choice().interface);
}

} // namespace studio
