#include "views/Palettes.h"

#include <QObject>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{

#include "views/PaletteData.inc"

std::vector<QColor> colours(const std::vector<const char *> &hex)
{
    std::vector<QColor> out;
    for (const char *h : hex)
    {
        out.emplace_back(QString::fromLatin1(h));
    }
    return out;
}

/// The studio's default: Okabe and Ito (2008), distinguishable under the
/// common colour vision deficiencies, blue first.
std::vector<QColor> okabeIto()
{
    return {QColor(0x2F, 0x6D, 0xB5), QColor(0xD5, 0x5E, 0x00), QColor(0x00, 0x9E, 0x73),
            QColor(0xCC, 0x79, 0xA7), QColor(0xE6, 0x9F, 0x00), QColor(0x56, 0xB4, 0xE9)};
}

std::vector<Palette> &registry()
{
    static std::vector<Palette> all = []() {
        std::vector<Palette> p;
        const auto qualitative = [&](const QString &id, const QString &name, const QString &family,
                                     const QString &source, std::vector<QColor> c) {
            Palette x;
            x.id = id;
            x.name = name;
            x.family = family;
            x.source = source;
            x.kind = Palette::Kind::Qualitative;
            x.colours = std::move(c);
            p.push_back(std::move(x));
        };
        qualitative(QStringLiteral("studio"), QObject::tr("Okabe–Ito (default)"), QStringLiteral("BayesTS Studio"),
                    QObject::tr("Okabe and Ito (2008), safe for colour vision deficiencies"), okabeIto());

        // ggplot2: the default hue palette and the ColorBrewer sets
        // scale_colour_brewer() offers.
        {
            Palette x;
            x.id = QStringLiteral("ggplot2-hue");
            x.name = QObject::tr("Hue (ggplot2 default)");
            x.family = QStringLiteral("ggplot2");
            x.source = QObject::tr("scales::hue_pal(), MIT");
            x.kind = Palette::Kind::ByCount;
            for (const auto &row : kGgplotHue)
            {
                x.byCount.push_back(colours(row));
            }
            p.push_back(std::move(x));
        }
        const QString brewer = QObject::tr("ColorBrewer, Cynthia A. Brewer, Apache License 2.0");
        qualitative(QStringLiteral("brewer-set1"), QStringLiteral("Set1"), QStringLiteral("ggplot2"), brewer, colours(kBrewerSet1));
        qualitative(QStringLiteral("brewer-set2"), QStringLiteral("Set2"), QStringLiteral("ggplot2"), brewer, colours(kBrewerSet2));
        qualitative(QStringLiteral("brewer-dark2"), QStringLiteral("Dark2"), QStringLiteral("ggplot2"), brewer, colours(kBrewerDark2));
        qualitative(QStringLiteral("brewer-paired"), QStringLiteral("Paired"), QStringLiteral("ggplot2"), brewer, colours(kBrewerPaired));

        // matplotlib's qualitative cycles.
        const QString mpl = QObject::tr("matplotlib, Matplotlib Development Team, matplotlib licence");
        qualitative(QStringLiteral("mpl-tab10"), QObject::tr("tab10 (matplotlib default)"), QStringLiteral("matplotlib"), mpl, colours(kMplTab10));
        qualitative(QStringLiteral("mpl-tab20"), QStringLiteral("tab20"), QStringLiteral("matplotlib"), mpl, colours(kMplTab20));
        qualitative(QStringLiteral("mpl-tab20b"), QStringLiteral("tab20b"), QStringLiteral("matplotlib"), mpl, colours(kMplTab20b));
        qualitative(QStringLiteral("mpl-tab20c"), QStringLiteral("tab20c"), QStringLiteral("matplotlib"), mpl, colours(kMplTab20c));
        qualitative(QStringLiteral("mpl-petroff10"), QObject::tr("petroff10 (accessible)"), QStringLiteral("matplotlib"),
                    mpl + QObject::tr("; Petroff (2021)"), colours(kMplPetroff10));

        // The viridis family, sampled evenly.
        const QString vir = QObject::tr("viridisLite, MIT; the maps by Smith, van der Walt and Firing, CC0");
        for (const auto &[id, name, data] : {std::tuple{"viridis", "viridis", &kViridis},
                                             std::tuple{"magma", "magma", &kMagma},
                                             std::tuple{"inferno", "inferno", &kInferno},
                                             std::tuple{"plasma", "plasma", &kPlasma},
                                             std::tuple{"cividis", "cividis", &kCividis}})
        {
            Palette x;
            x.id = QString::fromLatin1(id);
            x.name = QString::fromLatin1(name);
            x.family = QStringLiteral("viridis");
            x.source = QString::fromLatin1(id) == QLatin1String("cividis")
                           ? QObject::tr("viridisLite, MIT; Nuñez, Anderton and Renslow (2018)")
                           : vir;
            x.kind = Palette::Kind::Sequential;
            x.colours = colours(*data);
            p.push_back(std::move(x));
        }

        Palette custom;
        custom.id = QStringLiteral("custom");
        custom.name = QObject::tr("My palette");
        custom.family = QStringLiteral("Custom");
        custom.source = QObject::tr("Your own colours");
        custom.kind = Palette::Kind::Qualitative;
        p.push_back(std::move(custom));
        return p;
    }();
    return all;
}

int &currentIndex()
{
    static int index = [] {
        const QString id = QSettings().value(QStringLiteral("plots/palette"), QStringLiteral("studio")).toString();
        const auto &all = registry();
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            if (all[i].id == id)
            {
                return static_cast<int>(i);
            }
        }
        return 0;
    }();
    return index;
}

} // namespace

QColor Palette::colour(int index, int count) const
{
    switch (kind)
    {
    case Kind::Qualitative:
        if (colours.empty())
        {
            return okabeIto()[static_cast<std::size_t>(index) % 6];
        }
        return colours[static_cast<std::size_t>(std::max(0, index)) % colours.size()];
    case Kind::ByCount:
    {
        const int n = std::clamp(count > 0 ? count : 6, 1, static_cast<int>(byCount.size()));
        const auto &row = byCount[static_cast<std::size_t>(n - 1)];
        return row[static_cast<std::size_t>(std::max(0, index)) % row.size()];
    }
    case Kind::Sequential:
    {
        // Evenly over the map, short of its lightest end, which a white page
        // would swallow.
        const int n = std::max(count > 0 ? count : 6, index + 1);
        const double t = n <= 1 ? 0.0 : 0.88 * static_cast<double>(index) / static_cast<double>(n - 1);
        const double pos = t * static_cast<double>(colours.size() - 1);
        const auto lo = static_cast<std::size_t>(std::floor(pos));
        const std::size_t hi = std::min(lo + 1, colours.size() - 1);
        const double w = pos - static_cast<double>(lo);
        const QColor a = colours[lo], b = colours[hi];
        return QColor::fromRgbF(static_cast<float>(a.redF() + w * (b.redF() - a.redF())),
                                static_cast<float>(a.greenF() + w * (b.greenF() - a.greenF())),
                                static_cast<float>(a.blueF() + w * (b.blueF() - a.blueF())));
    }
    }
    return Qt::black;
}

const std::vector<Palette> &palettes()
{
    auto &all = registry();
    all.back().colours = customColours();
    return all;
}

const Palette &currentPalette()
{
    return palettes()[static_cast<std::size_t>(currentIndex())];
}

void setCurrentPalette(const QString &id, bool persist)
{
    const auto &all = palettes();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].id == id)
        {
            currentIndex() = static_cast<int>(i);
            if (persist)
            {
                QSettings().setValue(QStringLiteral("plots/palette"), id);
            }
        }
    }
}

std::vector<QColor> customColours()
{
    std::vector<QColor> out;
    for (const QString &c : QSettings().value(QStringLiteral("plots/customPalette")).toStringList())
    {
        if (QColor(c).isValid())
        {
            out.emplace_back(c);
        }
    }
    return out.empty() ? okabeIto() : out;
}

void setCustomColours(const std::vector<QColor> &colours)
{
    QStringList names;
    for (const QColor &c : colours)
    {
        names << c.name();
    }
    QSettings().setValue(QStringLiteral("plots/customPalette"), names);
}

} // namespace studio
