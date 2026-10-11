#include "app/MainWindow.h"
#include "app/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
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
            QCoreApplication::exit(ok ? 0 : 1);
        });
        QTimer::singleShot(300, &window, [&window, timer]() {
            window.runAll();
            timer->start(1500);
        });
    }
    return app.exec();
}
