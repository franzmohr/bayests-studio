#include "app/MainWindow.h"
#include "app/Theme.h"
#include "views/BuilderDialog.h"

#include <QAbstractButton>
#include <QDir>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QTabWidget>
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
                                // =zero: investment shock zero on income and
                                // positive on investment at impact.
                                if (qgetenv("STUDIO_SMOKE_SIGN") == "zero")
                                {
                                    for (QPushButton *ab : dialog->findChildren<QPushButton *>())
                                    {
                                        if (ab->text() == QStringLiteral("Add Restriction"))
                                        {
                                            ab->click();
                                        }
                                    }
                                    if (auto *table = dialog->findChild<QTableWidget *>())
                                    {
                                        qobject_cast<QComboBox *>(table->cellWidget(0, 1))->setCurrentIndex(1);
                                        qobject_cast<QComboBox *>(table->cellWidget(0, 2))->setCurrentIndex(2);
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
