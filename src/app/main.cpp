// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "app/MainWindow.h"
#include "app/Theme.h"
#include "views/AssistantDialogs.h"
#include "views/BuilderDialog.h"
#include "views/Workbench.h"
#include "engine/Extensions.h"
#include "views/FigureExport.h"
#include "views/ExtensionsDialog.h"
#include "views/FontDialog.h"
#include "views/Fonts.h"
#include "views/PaletteDialog.h"
#include "views/Palettes.h"
#include "views/TableExport.h"

#include <QAbstractButton>
#include <QDir>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>

#include <cstdio>
#include <QFile>
#include <QFontMetrics>
#include <QLocale>
#include <QTableWidget>
#include <QTabWidget>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimer>
#include <QIcon>
#include <QSettings>
#include <QMenuBar>
#include <QToolBar>
#include <qwt_text.h>
#include <qwt_plot.h>
#include <QAbstractScrollArea>
#include <QToolButton>
#include <QTabBar>
#include <QDockWidget>
#include <QDialogButtonBox>

#include <cstdlib>

#if defined(_OPENMP)
#include <omp.h>
#endif

extern "C"
{
void openblas_set_num_threads(int num_threads);
}

namespace studio
{
void runAssistantSmoke(MainWindow &window, const QString &which, const QString &folder);
}

namespace
{

/// A stage page's tabs by name, not the workbench's copies of them.
QTabWidget *stageTabs(QWidget &window, const QString &name)
{
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>(name))
    {
        bool inWorkbench = false;
        for (QWidget *w = tabs->parentWidget(); w != nullptr; w = w->parentWidget())
        {
            inWorkbench = inWorkbench || w->inherits("studio::Workbench");
        }
        if (!inWorkbench)
        {
            return tabs;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char *argv[])
{
    // Single-threaded unless the user says otherwise, as for the command line:
    // a chain is reproducible from its seed only then.
    if (const char *omp = std::getenv("OMP_NUM_THREADS"); omp == nullptr || *omp == '\0')
    {
#if defined(_OPENMP)
        omp_set_num_threads(1);
#endif
    }
    if (const char *ob = std::getenv("OPENBLAS_NUM_THREADS"); ob == nullptr || *ob == '\0')
    {
        openblas_set_num_threads(1);
    }

    QApplication app(argc, argv);
    // Numbers as R, the tables and a LaTeX document write them -- a decimal
    // point and no group separator -- whatever the system's locale: the axis
    // labels would otherwise read "0,6" and "1 970" beside tables that read
    // "0.6", and so would every exported figure.
    QLocale::setDefault(QLocale::c());
    QApplication::setOrganizationName(QStringLiteral("Franz X. Mohr"));
    QApplication::setOrganizationDomain(QStringLiteral("franzmohr.github.io"));
    QApplication::setApplicationName(QStringLiteral("BayesTS Studio"));
    QApplication::setApplicationDisplayName(QStringLiteral("BayesTS Studio"));
    QApplication::setApplicationVersion(QStringLiteral(STUDIO_VERSION));
    // STUDIO_SETTINGS=<folder> keeps the settings in an INI file there rather
    // than the registry: a portable install, or a test that must not touch
    // the user's own.
    if (const QString dir = qEnvironmentVariable("STUDIO_SETTINGS"); !dir.isEmpty())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    }

    studio::applyTheme(app);
    {
        // The window icon in each size drawn for it (tools/make_icon.py), so
        // a title bar gets the 16-pixel drawing rather than a reduction.
        QIcon icon;
        for (const int n : {16, 24, 32, 48, 64, 128, 256})
        {
            icon.addFile(QStringLiteral(":/icons/bayests-studio-%1.png").arg(n), QSize(n, n));
        }
        QApplication::setWindowIcon(icon);
    }
    studio::registerStudioFonts();   // before anything asks for a font
    studio::applySavedFonts();
    // The extensions' examples and palettes, before anything lists either.
    studio::loadExtensions(studio::defaultExtensionFiles());
    // Development aid: STUDIO_SMOKE_FONT=<family>[,<figure family>] sets the
    // interface's font, and the figures' where a second is given, for this run.
    if (const QByteArray font = qgetenv("STUDIO_SMOKE_FONT"); !font.isEmpty())
    {
        const QStringList families = QString::fromLocal8Bit(font).split(QLatin1Char(','));
        studio::FontChoice c = studio::currentFonts();
        c.systemInterface = false;
        c.interface = QFont(families.value(0), static_cast<int>(studio::systemFont().pointSizeF()));
        c.figureFamily = families.value(1);
        studio::setFonts(c, false);
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QApplication::tr("Model file or project folder to open."));
    parser.process(app);

    studio::MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty())
    {
        window.openPath(parser.positionalArguments().constFirst());
    }

    // Development aid: STUDIO_SMOKE_PALETTE=<id> draws in that palette.
    if (const QByteArray palette = qgetenv("STUDIO_SMOKE_PALETTE"); !palette.isEmpty())
    {
        studio::setCurrentPalette(QString::fromLatin1(palette), false);
    }

    // Development aid: STUDIO_SMOKE_TOOLTIPS=<csv> opens every dialog with a
    // form on that data and names each input without a tooltip, then exits:
    // 1 if any lacks one.
    if (const QByteArray csv = qgetenv("STUDIO_SMOKE_TOOLTIPS"); !csv.isEmpty())
    {
        const studio::DataSet data = studio::readDelimited(QString::fromLocal8Bit(csv));
        int missing = 0, inputs = 0;
        // The inputs; and in the views also every plot and every button but a
        // dialog's own OK and Cancel and the parts of Qt's composite widgets.
        const auto audit = [&](QWidget *dialog, const char *name, bool views = false) {
            QList<QWidget *> fields;
            for (QWidget *w : dialog->findChildren<QWidget *>())
            {
                const QWidget *parent = w->parentWidget();
                const bool part = qobject_cast<const QAbstractSpinBox *>(parent) ||
                                  qobject_cast<const QComboBox *>(parent) ||
                                  qobject_cast<const QDialogButtonBox *>(parent) ||
                                  qobject_cast<const QTabBar *>(parent) ||
                                  qobject_cast<const QAbstractScrollArea *>(parent) ||
                                  qobject_cast<const QDockWidget *>(parent) ||
                                  qobject_cast<const QToolBar *>(parent) ||   // an action's own tooltip
                                  qobject_cast<const QMenuBar *>(parent);
                if (part)
                {
                    continue;
                }
                if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w) ||
                    qobject_cast<QCheckBox *>(w) || qobject_cast<QRadioButton *>(w) ||
                    qobject_cast<QListWidget *>(w) || qobject_cast<QLineEdit *>(w) ||
                    (views && (qobject_cast<QwtPlot *>(w) || qobject_cast<QPushButton *>(w) ||
                               qobject_cast<QToolButton *>(w))))
                {
                    fields << w;
                }
            }
            for (QWidget *w : fields)
            {
                ++inputs;
                if (w->toolTip().isEmpty())
                {
                    ++missing;
                    QString what = w->objectName();
                    if (auto *b = qobject_cast<QAbstractButton *>(w))
                    {
                        what = b->text();
                    }
                    if (auto *plot = qobject_cast<QwtPlot *>(w))
                    {
                        what = QStringLiteral("plot: ") + plot->axisTitle(QwtPlot::yLeft).text();
                    }
                    if (w->parentWidget())
                    {
                        what += QStringLiteral(" in %1").arg(QString::fromLatin1(w->parentWidget()->metaObject()->className()));
                    }
                    // The view it belongs to: the nearest of the studio's own classes.
                    for (const QWidget *up = w->parentWidget(); up; up = up->parentWidget())
                    {
                        if (QByteArray(up->metaObject()->className()).startsWith("studio::"))
                        {
                            what += QStringLiteral("  [%1]").arg(QString::fromLatin1(up->metaObject()->className()));
                            break;
                        }
                    }
                    std::printf("no tooltip  %s: %s %s\n", name, w->metaObject()->className(),
                                what.toLocal8Bit().constData());
                }
            }
        };
        {
            studio::BuilderDialog d(data, QDir::tempPath());
            audit(&d, "builder");
        }
        {
            studio::ThesisBaselineDialog d(data, nullptr);
            audit(&d, "thesis baseline");
        }
        {
            studio::HorseRaceDialog d(data, nullptr);
            audit(&d, "horse race");
        }
        {
            studio::FontDialog d;
            audit(&d, "font");
        }
        {
            studio::PaletteDialog d;
            audit(&d, "palette");
        }
        {
            studio::MainWindow w;
            w.setAssistantEnabled(true);   // optional, and off by default: audited all the same
            audit(&w, "views", true);
        }
        // The licence Help > Licence shows.
        if (QFile licence(QStringLiteral(":/licence/LICENSE")); !licence.open(QIODevice::ReadOnly) ||
                                                                licence.size() < 1000)
        {
            std::printf("the licence is not in the resources\n");
            ++missing;
        }
        // The fonts the studio ships, registered and in use: Inter for the
        // interface and the guidance alike, sans serif, and every character the
        // studio writes drawn from them rather than a system fallback.
        {
            const QFont ui = QApplication::font();
            const QFont reading = studio::readingFont();
            const QFontMetrics metrics(ui);
            const QString used = QStringLiteral("ρκ₁×−–—…‘’“”≤≥±·µσ√∑→←○●★⚠✓✗äöüÄÖÜßéèñçàíóúâêîôûëïÿ°§²³");
            int lacking = 0;
            for (const char32_t c : used.toUcs4())
            {
                lacking += metrics.inFontUcs4(c) ? 0 : 1;
            }
            if (ui.family() != QLatin1String("Inter") ||
                reading.family() != QLatin1String("Inter") || lacking > 0)
            {
                std::printf("the studio's fonts are not in use: interface %s, reading %s, %d characters lacking\n",
                            ui.family().toStdString().c_str(), reading.families().join(QLatin1Char(',')).toStdString().c_str(),
                            lacking);
                ++missing;
            }
        }
        // And the window icon, every size of it from the resources.
        const int iconSizes = static_cast<int>(QApplication::windowIcon().availableSizes().size());
        if (iconSizes != 7)
        {
            std::printf("window icon: %d of 7 sizes\n", iconSizes);
            ++missing;
        }
        std::printf("smoke: %d of %d inputs without a tooltip\n", missing, inputs);
        return missing == 0 ? 0 : 1;
    }
    // Development aid: STUDIO_SMOKE_FONT_DIALOG=<png> draws the font dialog and exits.
    if (const QByteArray png = qgetenv("STUDIO_SMOKE_FONT_DIALOG"); !png.isEmpty())
    {
        studio::FontDialog dialog;
        dialog.show();
        QApplication::processEvents();
        const bool ok = dialog.grab().save(QString::fromLocal8Bit(png));
        std::printf("smoke: font dialog %s\n", ok ? "saved" : "NOT saved");
        return ok ? 0 : 1;
    }
    // Development aid: STUDIO_SMOKE_OWN_PALETTES=1 checks the user's own
    // palettes (with STUDIO_SETTINGS, so as not to touch the real ones): an
    // earlier version's single palette is migrated, several are kept and
    // listed, and the current one survives the list being built again.
    if (!qgetenv("STUDIO_SMOKE_OWN_PALETTES").isEmpty())
    {
        if (qEnvironmentVariable("STUDIO_SETTINGS").isEmpty())
        {
            std::printf("smoke: own palettes need STUDIO_SETTINGS\n");
            return 2;
        }
        int bad = 0;
        const auto check = [&bad](bool ok, const char *what) {
            std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
            bad += ok ? 0 : 1;
        };
        {
            QSettings settings;
            settings.clear();
            settings.setValue(QStringLiteral("plots/customPalette"), QStringList{QStringLiteral("#112233"), QStringLiteral("#445566")});
            settings.setValue(QStringLiteral("plots/palette"), QStringLiteral("custom"));
        }
        const std::vector<studio::OwnPalette> old = studio::ownPalettes();
        check(old.size() == 1 && old[0].colours.size() == 2 && old[0].colours[1] == QColor(0x44, 0x55, 0x66),
              "an earlier version's palette becomes the first of one's own");
        studio::setOwnPalettes({{QStringLiteral("House style"), {QColor(Qt::red), QColor(Qt::blue)}},
                                {QStringLiteral("Journal"), {QColor(Qt::black), QColor(Qt::gray), QColor(Qt::white)}}});
        int own = 0;
        for (const studio::Palette &p : studio::palettes())
        {
            own += p.family == studio::ownPaletteFamily() ? 1 : 0;
        }
        check(own == 2 && studio::ownPalettes().size() == 2 && !QSettings().contains(QStringLiteral("plots/customPalette")),
              "two of one's own kept and listed under My organisation's colours");
        studio::setCurrentPalette(studio::ownPaletteId(QStringLiteral("Journal")));
        studio::reloadPalettes();
        check(studio::currentPalette().id == studio::ownPaletteId(QStringLiteral("Journal")) &&
                  studio::currentPalette().colours.size() == 3,
              "the current palette survives the list being built again");
        studio::setOwnPalettes({{QStringLiteral("House style"), {QColor(Qt::red), QColor(Qt::blue)}}});
        check(studio::currentPalette().id == QStringLiteral("studio"),
              "deleting the current one falls back to the studio's default");
        std::printf(bad == 0 ? "PASS\n" : "%d FAILURES\n", bad);
        return bad == 0 ? 0 : 1;
    }
    // Development aid: STUDIO_SMOKE_EXTENSIONS_DIALOG=<png> draws Help >
    // Extensions as the start-up load left it, and exits.
    if (const QByteArray png = qgetenv("STUDIO_SMOKE_EXTENSIONS_DIALOG"); !png.isEmpty())
    {
        studio::ExtensionsDialog dialog;
        dialog.show();
        QApplication::processEvents();
        const bool ok = dialog.grab().save(QString::fromLocal8Bit(png));
        std::printf("smoke: extensions dialog %s\n", ok ? "saved" : "NOT saved");
        return ok ? 0 : 1;
    }
    // Development aid: STUDIO_SMOKE_PALETTE_DIALOG=<png> draws the palette
    // dialog, in the palette chosen above, and exits.
    if (const QByteArray png = qgetenv("STUDIO_SMOKE_PALETTE_DIALOG"); !png.isEmpty())
    {
        studio::PaletteDialog dialog;
        dialog.show();
        QApplication::processEvents();
        const bool ok = dialog.grab().save(QString::fromLocal8Bit(png));
        std::printf("smoke: palette dialog %s\n", ok ? "saved" : "NOT saved");
        return ok ? 0 : 1;
    }

    // Development aid: STUDIO_SMOKE_WORKBENCH=<prefix> shows the workbench on a
    // wide screen with the data STUDIO_SMOKE_DATA names imported, captures it
    // as <prefix>-specify.png, estimates the builder's default model into the
    // folder given and captures <prefix>-estimated.png once the panels are drawn.
    if (const QByteArray prefix = qgetenv("STUDIO_SMOKE_WORKBENCH"); !prefix.isEmpty())
    {
        // STUDIO_SMOKE_SIZE=<w>x<h>: a smaller screen, a laptop's say, and
        // STUDIO_SMOKE_FOLDED=<pane>,...: the panes folded on it.
        const QStringList size = QString::fromLocal8Bit(qgetenv("STUDIO_SMOKE_SIZE")).split(QLatin1Char('x'));
        window.resize(size.size() == 2 ? size[0].toInt() : 2560, size.size() == 2 ? size[1].toInt() : 1300);
        window.setWorkbench(true);
        if (const QByteArray folded = qgetenv("STUDIO_SMOKE_FOLDED"); !folded.isEmpty())
        {
            window.workbench()->setFoldedPanes(QString::fromLocal8Bit(folded).split(QLatin1Char(',')));
        }
        const QString data = QString::fromLocal8Bit(qgetenv("STUDIO_SMOKE_DATA"));
        if (!data.isEmpty())
        {
            window.importFrom(data);
        }
        const QString base = QString::fromLocal8Bit(prefix);
        const auto settle = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
        };
        // STUDIO_SMOKE_TVP=1: time-varying coefficients on a short chain, and
        // the state variance ladder run once the model is estimated.
        const bool tvp = !qgetenv("STUDIO_SMOKE_TVP").isEmpty();
        QObject::connect(&window, &studio::MainWindow::runEnded, &window, [&window, base, settle, tvp](bool ok) {
            settle(tvp ? 3000 : 12000);
            if (tvp)
            {
                for (QPushButton *b : window.findChildren<QPushButton *>())
                {
                    if (b->text() == QObject::tr("Estimate ÷10, ×1, ×10") && b->isVisible())
                    {
                        b->click();
                        settle(1000);
                        QElapsedTimer t;
                        t.start();
                        while (!b->isEnabled() && t.elapsed() < 600000)
                        {
                            settle(500);
                        }
                        settle(2000);
                    }
                }
            }
            window.grab().save(base + QStringLiteral("-estimated.png"));
            // A time-varying model's impulse responses at its first period.
            if (tvp)
            {
                for (QComboBox *c : window.findChildren<QComboBox *>())
                {
                    if (c->isVisible() && c->count() > 1 &&
                        c->toolTip().startsWith(QObject::tr("For a model whose coefficients")))
                    {
                        c->setCurrentIndex(0);
                    }
                }
                settle(10000);
                window.grab().save(base + QStringLiteral("-irf-first.png"));
                // The variance decomposition at the first period, then the last.
                for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
                {
                    for (int i = 0; i < tabs->count(); ++i)
                    {
                        if (tabs->isVisible() && tabs->tabText(i) == QObject::tr("Variance decomposition"))
                        {
                            tabs->setCurrentIndex(i);
                        }
                    }
                }
                settle(4000);
                window.grab().save(base + QStringLiteral("-fevd-first.png"));
                for (QComboBox *c : window.findChildren<QComboBox *>())
                {
                    if (c->isVisible() && c->count() > 1 &&
                        c->toolTip().startsWith(QObject::tr("For a model whose coefficients")))
                    {
                        c->setCurrentIndex(c->count() - 1);
                    }
                }
                settle(10000);
                window.grab().save(base + QStringLiteral("-fevd-last.png"));
            }
            // STUDIO_SMOKE_BLOCK=1: the draws as whole blocks, a/coeffs and
            // then the error precision.
            if (!qgetenv("STUDIO_SMOKE_BLOCK").isEmpty())
            {
                // A time-varying model opens on its paths: the draws' tab first.
                for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
                {
                    for (int i = 0; i < tabs->count(); ++i)
                    {
                        if (tabs->isVisible() && tabs->tabText(i) == QObject::tr("Draws and convergence"))
                        {
                            tabs->setCurrentIndex(i);
                        }
                    }
                }
                settle(500);
                for (QComboBox *c : window.findChildren<QComboBox *>())
                {
                    if (c->isVisible() && c->findText(QObject::tr("Whole block")) >= 0)
                    {
                        c->setCurrentIndex(c->findText(QObject::tr("Whole block")));
                    }
                }
                settle(1500);
                window.grab().save(base + QStringLiteral("-block-a.png"));
                // A path's block at its first period too.
                for (QComboBox *c : window.findChildren<QComboBox *>())
                {
                    if (c->isVisible() && c->toolTip().startsWith(QObject::tr("The period at which")))
                    {
                        c->setCurrentIndex(0);
                        settle(1500);
                        window.grab().save(base + QStringLiteral("-block-a-first.png"));
                    }
                }
                for (QComboBox *c : window.findChildren<QComboBox *>())
                {
                    if (c->isVisible() && c->findText(QStringLiteral("u_sigma_inv/coeffs")) >= 0)
                    {
                        c->setCurrentIndex(c->findText(QStringLiteral("u_sigma_inv/coeffs")));
                    }
                }
                settle(1500);
                window.grab().save(base + QStringLiteral("-block-sigma.png"));
                for (QComboBox *c : window.findChildren<QComboBox *>())
                {
                    if (c->isVisible() && c->findText(QStringLiteral("psi/coeffs")) >= 0)
                    {
                        c->setCurrentIndex(c->findText(QStringLiteral("psi/coeffs")));
                        settle(1500);
                        window.grab().save(base + QStringLiteral("-block-psi.png"));
                    }
                }
            }
            QCoreApplication::exit(ok ? 0 : 1);
        });
        QTimer::singleShot(800, &window, [&window, base, tvp]() {
            // STUDIO_SMOKE_EXO=<series>: that series weakly exogenous, a VARX,
            // with no forecast horizon -- its future values are not in the data.
            if (const QString exo = QString::fromLocal8Bit(qgetenv("STUDIO_SMOKE_EXO")); !exo.isEmpty())
            {
                for (QListWidget *list : window.findChildren<QListWidget *>())
                {
                    if (!list->isVisible())
                    {
                        continue;
                    }
                    const bool exogenous = list->toolTip().startsWith(QObject::tr("Regressors the model does not"));
                    const bool endogenous = list->toolTip().startsWith(QObject::tr("The variables the model explains"));
                    for (int i = 0; (exogenous || endogenous) && i < list->count(); ++i)
                    {
                        if (list->item(i)->text() == exo)
                        {
                            list->item(i)->setCheckState(exogenous ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                }
                for (QSpinBox *b : window.findChildren<QSpinBox *>())
                {
                    if (b->isVisible() && b->toolTip().contains(QObject::tr("forecast"), Qt::CaseInsensitive) &&
                        b->value() == 8)
                    {
                        b->setValue(0);
                    }
                }
                QCoreApplication::processEvents();
            }
            if (tvp)
            {
                for (QRadioButton *r : window.findChildren<QRadioButton *>())
                {
                    if (r->text().startsWith(QObject::tr("Time-varying")))
                    {
                        r->setChecked(true);
                    }
                }
                // STUDIO_SMOKE_COVAR=1: gamma errors with a covariance block,
                // whose Psi then varies over time too.
                if (!qgetenv("STUDIO_SMOKE_COVAR").isEmpty())
                {
                    for (QRadioButton *r : window.findChildren<QRadioButton *>())
                    {
                        if (r->text() == QObject::tr("Gamma"))
                        {
                            r->setChecked(true);
                        }
                    }
                    QCoreApplication::processEvents();
                    for (QCheckBox *c : window.findChildren<QCheckBox *>())
                    {
                        if (c->text().startsWith(QObject::tr("Covariance block")))
                        {
                            c->setChecked(true);
                        }
                    }
                }
                for (QSpinBox *b : window.findChildren<QSpinBox *>())
                {
                    if (b->value() == 20000)
                    {
                        b->setValue(1000);   // draws kept
                    }
                    else if (b->value() == 2000)
                    {
                        b->setValue(1000);   // burn-in
                    }
                }
                QCoreApplication::processEvents();
            }
            window.grab().save(base + QStringLiteral("-specify.png"));
            for (QPushButton *b : window.findChildren<QPushButton *>())
            {
                if (b->text() == QObject::tr("Estimate as a New Model"))
                {
                    // A message box the click raises is captured, then closed.
                    QTimer::singleShot(3000, &window, [base]() {
                        if (QWidget *modal = QApplication::activeModalWidget())
                        {
                            modal->grab().save(base + QStringLiteral("-message.png"));
                            modal->close();
                            QCoreApplication::exit(1);
                        }
                    });
                    b->click();
                    return;
                }
            }
            std::printf("smoke: no estimate button\n");
            QCoreApplication::exit(1);
        });
        return app.exec();
    }

    // Development aid: STUDIO_SMOKE_ASSISTANT drives the assistant through a
    // workflow (AssistantSmoke.cpp).
    if (const QByteArray which = qgetenv("STUDIO_SMOKE_ASSISTANT"); !which.isEmpty())
    {
        window.resize(1700, 1000);
        const QString folder = QString::fromLocal8Bit(qgetenv("STUDIO_SMOKE_FOLDER"));
        QTimer::singleShot(300, &window, [&window, which, folder]() {
            studio::runAssistantSmoke(window, QString::fromLocal8Bit(which), folder);
        });
        return app.exec();
    }

    // Development aid, not a feature: STUDIO_SMOKE_CAPTURE=<prefix> runs every
    // stage of the file given and saves window captures during and after the
    // run as <prefix>-<n>.png, then quits with the run's status. Works under
    // QT_QPA_PLATFORM=offscreen, so it needs no display.
    if (const QByteArray prefix = qgetenv("STUDIO_SMOKE_CAPTURE"); !prefix.isEmpty())
    {
        window.resize(1400, 860);
        int shot = 0;
        auto *timer = new QTimer(&window);
        QObject::connect(timer, &QTimer::timeout, &window, [&window, prefix, &shot]() {
            window.grab().save(QStringLiteral("%1-%2.png").arg(QString::fromLocal8Bit(prefix)).arg(++shot));
        });
        QObject::connect(&window, &studio::MainWindow::runEnded, &window, [&window, prefix, timer](bool ok) {
            timer->stop();
            // One capture per stage of the finished model.
            for (int stage = 0; stage < 5; ++stage)
            {
                window.showStage(static_cast<studio::StageId>(stage));
                QCoreApplication::processEvents();
                window.grab().save(QStringLiteral("%1-stage%2.png").arg(QString::fromLocal8Bit(prefix)).arg(stage + 1));
            }
            // The structural analysis: impulse responses, then the
            // variance decomposition, each once its worker has finished.
            const auto settle = [](int ms) {
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
            };
            // STUDIO_SMOKE_SUMMARY=1: the summary plot, every block, then
            // the first block as a trace and a box plot.
            if (!qgetenv("STUDIO_SMOKE_SUMMARY").isEmpty())
            {
                window.showStage(studio::StageId::Posterior);
                if (auto *tabs = stageTabs(window, QStringLiteral("posteriorTabs")))
                {
                    tabs->setCurrentIndex(2);
                    settle(4000);
                    auto *block = window.findChild<QComboBox *>(QStringLiteral("summaryBlock"));
                    auto *type = window.findChild<QComboBox *>(QStringLiteral("summaryType"));
                    for (int b = 0; block != nullptr && b < block->count(); ++b)
                    {
                        block->setCurrentIndex(b);
                        settle(400);
                        window.grab().save(QStringLiteral("%1-summary%2.png").arg(QString::fromLocal8Bit(prefix)).arg(b + 1));
                    }
                    if (block != nullptr && type != nullptr)
                    {
                        block->setCurrentIndex(0);
                        for (int t = 1; t < type->count(); ++t)
                        {
                            type->setCurrentIndex(t);
                            settle(400);
                            window.grab().save(QStringLiteral("%1-summary-type%2.png").arg(QString::fromLocal8Bit(prefix)).arg(t + 1));
                        }
                    }
                }
                window.showStage(studio::StageId::Application);
            }
            // STUDIO_SMOKE_INCLUSION=1: the inclusion probabilities.
            if (!qgetenv("STUDIO_SMOKE_INCLUSION").isEmpty())
            {
                window.showStage(studio::StageId::Posterior);
                if (auto *tabs = stageTabs(window, QStringLiteral("posteriorTabs")))
                {
                    tabs->setCurrentIndex(3);
                    settle(4000);
                    window.grab().save(QStringLiteral("%1-inclusion.png").arg(QString::fromLocal8Bit(prefix)));
                }
                window.showStage(studio::StageId::Application);
            }
            // STUDIO_SMOKE_VOLATILITY=1: the volatility break.
            if (!qgetenv("STUDIO_SMOKE_VOLATILITY").isEmpty())
            {
                window.showStage(studio::StageId::Posterior);
                if (auto *tabs = stageTabs(window, QStringLiteral("posteriorTabs")))
                {
                    tabs->setCurrentIndex(4);
                    settle(3000);
                    window.grab().save(QStringLiteral("%1-volatility.png").arg(QString::fromLocal8Bit(prefix)));
                }
                window.showStage(studio::StageId::Application);
            }
            if (auto *app = stageTabs(window, QStringLiteral("applicationTabs")))
            {
                app->setCurrentIndex(1);
                settle(3000);
                window.grab().save(QStringLiteral("%1-irf.png").arg(QString::fromLocal8Bit(prefix)));
                if (auto *st = window.findChild<QTabWidget *>(QStringLiteral("structuralTabs")))
                {
                    st->setCurrentIndex(1);
                    settle(500);
                    window.grab().save(QStringLiteral("%1-fevd.png").arg(QString::fromLocal8Bit(prefix)));
                    // And one variable's: a panel per shock, the stacked means last.
                    if (auto *response = st->findChild<QComboBox *>(QStringLiteral("fevdResponse"));
                        response && response->count() > 1)
                    {
                        response->setCurrentIndex(response->count() - 1);
                        settle(1500);
                        window.grab().save(QStringLiteral("%1-fevd-one.png").arg(QString::fromLocal8Bit(prefix)));
                        response->setCurrentIndex(0);
                    }
                    st->setCurrentIndex(0);
                }
                // STUDIO_SMOKE_MULTIPLIERS=1: the multipliers, permanent then
                // transitory.
                if (!qgetenv("STUDIO_SMOKE_MULTIPLIERS").isEmpty())
                {
                    app->setCurrentIndex(3);
                    settle(3000);
                    window.grab().save(QStringLiteral("%1-multipliers.png").arg(QString::fromLocal8Bit(prefix)));
                    for (QComboBox *c : app->widget(3)->findChildren<QComboBox *>())
                    {
                        if (c->count() == 2 && c->itemData(1).typeId() == QMetaType::Bool)
                        {
                            c->setCurrentIndex(1);
                        }
                    }
                    settle(2000);
                    window.grab().save(QStringLiteral("%1-multipliers-transitory.png").arg(QString::fromLocal8Bit(prefix)));
                    app->setCurrentIndex(1);
                }
                // STUDIO_SMOKE_HD=1: the historical decomposition, with and
                // without the baseline.
                if (!qgetenv("STUDIO_SMOKE_HD").isEmpty())
                {
                    app->setCurrentIndex(4);
                    settle(4000);
                    window.grab().save(QStringLiteral("%1-hd.png").arg(QString::fromLocal8Bit(prefix)));
                    for (QCheckBox *b : app->widget(4)->findChildren<QCheckBox *>())
                    {
                        b->setChecked(true);
                    }
                    settle(500);
                    window.grab().save(QStringLiteral("%1-hd-baseline.png").arg(QString::fromLocal8Bit(prefix)));
                    // And one variable's, the stacked bars.
                    if (auto *response = app->widget(4)->findChild<QComboBox *>(QStringLiteral("hdResponse"));
                        response && response->count() > 1)
                    {
                        response->setCurrentIndex(response->count() - 1);
                        settle(2000);
                        window.grab().save(QStringLiteral("%1-hd-one.png").arg(QString::fromLocal8Bit(prefix)));
                        response->setCurrentIndex(0);
                    }
                    app->setCurrentIndex(1);
                }
                // STUDIO_SMOKE_SPILLOVER=1: every tab of the spillovers.
                if (!qgetenv("STUDIO_SMOKE_SPILLOVER").isEmpty())
                {
                    app->setCurrentIndex(2);
                    settle(5000);
                    if (auto *sp = window.findChild<QTabWidget *>(QStringLiteral("spilloverTabs")))
                    {
                        for (int tab = 0; tab < sp->count(); ++tab)
                        {
                            sp->setCurrentIndex(tab);
                            settle(400);
                            window.grab().save(
                                QStringLiteral("%1-spillover%2.png").arg(QString::fromLocal8Bit(prefix)).arg(tab + 1));
                        }
                    }
                    app->setCurrentIndex(1);
                }
                // STUDIO_SMOKE_SIGN=1: identify by the restriction the dialog
                // starts with, capturing the dialog and then the responses.
                if (!qgetenv("STUDIO_SMOKE_SIGN").isEmpty())
                {
                    for (QPushButton *b : window.findChildren<QPushButton *>())
                    {
                        if (b->text().startsWith(QStringLiteral("Sign Restrictions")) && b->isEnabled())
                        {
                            QTimer::singleShot(800, [prefix]() {
                                QWidget *dialog = QApplication::activeModalWidget();
                                if (dialog == nullptr)
                                {
                                    return;
                                }
                                // =zero: the first shock zero on the second
                                // variable at impact, typed into the grid as a
                                // user would, beside the + the dialog starts with.
                                if (qgetenv("STUDIO_SMOKE_SIGN") == "zero")
                                {
                                    if (auto *grid = dialog->findChild<QTableWidget *>(QStringLiteral("signGrid")))
                                    {
                                        grid->setFocus();
                                        grid->setCurrentCell(1, 0);
                                        QKeyEvent press(QEvent::KeyPress, Qt::Key_0, Qt::NoModifier, QStringLiteral("0"));
                                        QCoreApplication::sendEvent(grid, &press);
                                    }
                                    QCoreApplication::processEvents();
                                }
                                dialog->grab().save(QStringLiteral("%1-signdialog.png").arg(QString::fromLocal8Bit(prefix)));
                                for (QPushButton *ib : dialog->findChildren<QPushButton *>())
                                {
                                    if (ib->text() == QStringLiteral("Identify"))
                                    {
                                        ib->click();
                                    }
                                }
                                // A warning keeps the dialog open on "Done".
                                QTimer::singleShot(8000, [dialog]() {
                                    for (QPushButton *db : dialog->findChildren<QPushButton *>())
                                    {
                                        if (db->text() == QStringLiteral("Done"))
                                        {
                                            dialog->grab().save(QStringLiteral("signzero-warning.png"));
                                            db->click();
                                        }
                                    }
                                });
                            });
                            b->click();
                            settle(qgetenv("STUDIO_SMOKE_SIGN") == "zero" ? 6000 : 3000);
                            window.grab().save(QStringLiteral("%1-sign.png").arg(QString::fromLocal8Bit(prefix)));
                            break;
                        }
                    }
                }
            }
            // STUDIO_SMOKE_FIGURES=<directory>: every view's figure as PDF,
            // SVG and PNG, as its Export Figure button writes it.
            if (const QByteArray dir = qgetenv("STUDIO_SMOKE_FIGURES"); !dir.isEmpty())
            {
                int n = 0;
                for (const studio::FigureSpec &spec : studio::figureSources(&window))
                {
                    double w = 0.0, h = 0.0;
                    studio::defaultFigureSize(spec, w, h);
                    ++n;
                    for (const char *ext : {"pdf", "svg", "png"})
                    {
                        QString error;
                        const QString path = QStringLiteral("%1/%2-%3.%4")
                                                 .arg(QString::fromLocal8Bit(dir)).arg(n).arg(spec.name).arg(ext);
                        if (!studio::writeFigure(spec, path, w, h, 200, &error))
                        {
                            std::fprintf(stderr, "figure %s: %s\n", path.toLocal8Bit().constData(),
                                         error.toLocal8Bit().constData());
                        }
                    }
                }
            }
            // STUDIO_SMOKE_EXPORT=<file.tex>: every exportable table of the
            // window, as its Export menu writes it, in one LaTeX file.
            if (const QByteArray out = qgetenv("STUDIO_SMOKE_EXPORT"); !out.isEmpty())
            {
                QFile tex(QString::fromLocal8Bit(out));
                if (tex.open(QIODevice::WriteOnly))
                {
                    for (QTableWidget *t : window.findChildren<QTableWidget *>())
                    {
                        if (!t->property("exportCaption").toString().isEmpty())
                        {
                            tex.write(studio::toLatex(studio::tableContent(t)).toUtf8() + "\n");
                        }
                    }
                }
            }
            QCoreApplication::exit(ok ? 0 : 1);
        });
        // STUDIO_SMOKE_BUILDER=<csv> builds the model first: imports the data,
        // ticks a TVP-VAR with stochastic volatility and a covariance block in
        // the builder, captures it, and creates the file in the current folder.
        const QByteArray csv = qgetenv("STUDIO_SMOKE_BUILDER");
        QTimer::singleShot(300, &window, [&window, timer, csv, prefix]() {
            if (!csv.isEmpty())
            {
                window.importFrom(QString::fromLocal8Bit(csv));
                auto *dialog = new studio::BuilderDialog(window.workingData(), QDir::currentPath(), &window);
                dialog->show();
                // STUDIO_SMOKE_TICK names the controls to tick, by object name.
                QByteArray tick = qgetenv("STUDIO_SMOKE_TICK");
                if (tick.isEmpty())
                {
                    tick = "coefTvp,errSv,covar";
                }
                for (const QByteArray &name : tick.split(','))
                {
                    if (auto *b = dialog->findChild<QAbstractButton *>(QString::fromLatin1(name)))
                    {
                        b->setChecked(true);
                    }
                }
                // STUDIO_SMOKE_SELECTION=1 (BVS) or 2 (SSVS) picks the selection scheme.
                if (const QByteArray sel = qgetenv("STUDIO_SMOKE_SELECTION"); !sel.isEmpty())
                {
                    if (auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("selection")))
                    {
                        combo->setCurrentIndex(sel.toInt());
                    }
                }
                // STUDIO_SMOKE_BREAK names the period a volatility break starts.
                if (const QByteArray at = qgetenv("STUDIO_SMOKE_BREAK"); !at.isEmpty())
                {
                    if (auto *combo = dialog->findChild<QComboBox *>(QStringLiteral("breakPeriod")))
                    {
                        combo->setCurrentText(QString::fromLocal8Bit(at));
                    }
                }
                // STUDIO_SMOKE_EXOGENOUS names a series to tick as weakly exogenous.
                if (const QByteArray exo = qgetenv("STUDIO_SMOKE_EXOGENOUS"); !exo.isEmpty())
                {
                    if (auto *list = dialog->findChild<QListWidget *>(QStringLiteral("exogenousList")))
                    {
                        for (int i = 0; i < list->count(); ++i)
                        {
                            if (list->item(i)->text() == QString::fromLocal8Bit(exo))
                            {
                                list->item(i)->setCheckState(Qt::Checked);
                            }
                        }
                    }
                }
                QElapsedTimer wait;
                wait.start();
                while (wait.elapsed() < 1500)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                dialog->grab().save(QStringLiteral("%1-builder.png").arg(QString::fromLocal8Bit(prefix)));
                if (dialog->createAt(QDir::current().filePath(QStringLiteral("built.h5"))))
                {
                    window.addCreatedModel(dialog->createdFile());
                }
                dialog->deleteLater();
            }
            window.runAll();
            timer->start(1500);
        });
    }
    return app.exec();
}
