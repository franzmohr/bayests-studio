// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

// Development aid, not a feature: STUDIO_SMOKE_ASSISTANT=<workflow>:<example>
// drives the assistant through a whole workflow as a user would -- clicking
// its buttons, accepting its dialogs with what they propose, ticking the
// confirmations -- in the empty folder STUDIO_SMOKE_FOLDER, saving a capture
// of the window after every step as <folder>/shot-NN-<step>.png, and quits.

#include "app/MainWindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace studio
{

namespace
{

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

bool waitFor(const std::function<bool()> &done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    return done();
}

} // namespace

void runAssistantSmoke(MainWindow &window, const QString &which, const QString &folder)
{
    const QString workflow = which.section(QLatin1Char(':'), 0, 0);
    const QString example = which.section(QLatin1Char(':'), 1, 1);
    AssistantPanel *assistant = window.assistant();
    int n = 0;
    const auto shot = [&](const QString &name) {
        settle(600);
        window.grab().save(QDir(folder).filePath(QStringLiteral("shot-%1-%2.png").arg(++n, 2, 10, QLatin1Char('0')).arg(name)));
        std::printf("smoke: %s\n", name.toStdString().c_str());
        std::fflush(stdout);
    };
    // Clicks an action of the step shown; a dialog it opens is handled by
    // `dialog`, run from a timer inside the dialog's own event loop.
    const auto click = [&](const QString &id, const std::function<void(QDialog *)> &dialog = {}) {
        auto *b = assistant->findChild<QPushButton *>(QStringLiteral("action-%1").arg(id));
        if (b == nullptr)
        {
            std::printf("smoke: no action %s\n", id.toStdString().c_str());
            return false;
        }
        if (dialog)
        {
            QTimer::singleShot(1500, [dialog]() {
                if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                {
                    dialog(d);
                }
            });
        }
        b->click();
        return true;
    };
    const auto confirm = [&]() {
        if (auto *c = assistant->findChild<QCheckBox *>(QStringLiteral("assistantConfirm")); c && c->isVisible())
        {
            c->setChecked(true);
        }
    };
    const auto batchDone = [&]() {
        auto *bar = assistant->findChild<QProgressBar *>();
        return bar == nullptr || !bar->isVisible();
    };
    const auto accept = [&](QDialog *d) {
        d->grab().save(QDir(folder).filePath(QStringLiteral("dialog-%1.png").arg(n + 1, 2, 10, QLatin1Char('0'))));
        d->accept();
    };

    window.openPath(folder);
    settle(300);
    assistant->showChooser();
    shot(QStringLiteral("chooser"));
    assistant->start(workflow, example);
    shot(QStringLiteral("start"));
    click(QStringLiteral("import"));
    shot(QStringLiteral("data"));

    if (workflow == QLatin1String("thesis"))
    {
        confirm();
        shot(QStringLiteral("inspected"));
        click(QStringLiteral("baseline"), accept);
        shot(QStringLiteral("baseline"));
        click(QStringLiteral("runAll"));
        settle(500);
        waitFor([&]() { return !window.isRunning(); }, 900000);
        shot(QStringLiteral("estimated"));
        confirm();
        shot(QStringLiteral("converged"));
        // The optional lag comparison, run in the background as bayests processes.
        auto *steps = assistant->findChild<QListWidget *>(QStringLiteral("assistantSteps"));
        steps->setCurrentRow(5);
        click(QStringLiteral("lagVariants"));
        settle(500);
        waitFor(batchDone, 900000);
        shot(QStringLiteral("lags"));
        steps->setCurrentRow(6);
        click(QStringLiteral("exampleSigns"), [&](QDialog *d) {
            d->grab().save(QDir(folder).filePath(QStringLiteral("dialog-signs.png")));
            for (QPushButton *b : d->findChildren<QPushButton *>())
            {
                if (b->text() == QObject::tr("Identify"))
                {
                    b->click();
                }
            }
            // The importance sampler runs; a warning keeps the dialog open on Done.
            QTimer *poll = new QTimer(d);
            QObject::connect(poll, &QTimer::timeout, d, [d]() {
                for (QPushButton *b : d->findChildren<QPushButton *>())
                {
                    if (b->text() == QObject::tr("Done"))
                    {
                        b->click();
                    }
                }
            });
            poll->start(1000);
        });
        settle(1000);
        shot(QStringLiteral("identified"));
        confirm();
        steps->setCurrentRow(7);
        confirm();
        shot(QStringLiteral("responses"));
        steps->setCurrentRow(9);
        click(QStringLiteral("exportThesis"));
        shot(QStringLiteral("exported"));
    }
    else
    {
        click(QStringLiteral("designRace"), accept);
        shot(QStringLiteral("designed"));
        click(QStringLiteral("runRace"));
        settle(500);
        waitFor(batchDone, 3600000);
        shot(QStringLiteral("raced"));
        confirm();
        auto *steps = assistant->findChild<QListWidget *>(QStringLiteral("assistantSteps"));
        steps->setCurrentRow(4);
        confirm();
        shot(QStringLiteral("stability"));
        steps->setCurrentRow(5);
        click(QStringLiteral("exportRace"));
        shot(QStringLiteral("report"));
    }
    QCoreApplication::exit(0);
}

} // namespace studio
