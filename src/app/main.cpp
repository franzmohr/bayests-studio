#include "app/MainWindow.h"
#include "app/Theme.h"
#include "views/BuilderDialog.h"
#include "views/FigureExport.h"
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
#include <QLocale>
#include <QTableWidget>
#include <QTabWidget>
#include <QKeyEvent>
#include <QListWidget>
#include <QTimer>

#include <cstdlib>

#if defined(_OPENMP)
#include <omp.h>
#endif

extern "C"
{
void openblas_set_num_threads(int num_threads);
}

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

    studio::applyTheme(app);

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
                if (auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("posteriorTabs")))
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
                if (auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("posteriorTabs")))
                {
                    tabs->setCurrentIndex(3);
                    settle(4000);
                    window.grab().save(QStringLiteral("%1-inclusion.png").arg(QString::fromLocal8Bit(prefix)));
                }
                window.showStage(studio::StageId::Application);
            }
            if (auto *app = window.findChild<QTabWidget *>(QStringLiteral("applicationTabs")))
            {
                app->setCurrentIndex(1);
                settle(3000);
                window.grab().save(QStringLiteral("%1-irf.png").arg(QString::fromLocal8Bit(prefix)));
                if (auto *st = window.findChild<QTabWidget *>(QStringLiteral("structuralTabs")))
                {
                    st->setCurrentIndex(1);
                    settle(500);
                    window.grab().save(QStringLiteral("%1-fevd.png").arg(QString::fromLocal8Bit(prefix)));
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
