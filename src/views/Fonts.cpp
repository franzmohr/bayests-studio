#include "views/Fonts.h"

#include <QApplication>
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

QFont systemFont()
{
    if (!startFont())
    {
        startFont() = QApplication::font();
    }
    return *startFont();
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
    QApplication::setFont(kept.interface);
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
    systemFont();   // the platform's, before anything replaces it
    const FontChoice &c = choice();
    if (!c.systemInterface)
    {
        QApplication::setFont(c.interface);
    }
}

} // namespace studio
