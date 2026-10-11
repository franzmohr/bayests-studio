// A forecast horse race end to end: written from a data set, run by the
// batch runner as bayests processes, and scored.
//
//   studio_horserace_test <reference dir>
//
// <reference dir> holds e1.csv (test/exogenous_reference.R writes it). The race
// is written to <reference dir>/horserace, where test/horserace_reference.R
// recomputes every score with bvartools from the same files and compares them
// with scores.csv, which this test writes beside them.

#include "engine/BatchRunner.h"
#include "engine/DataSet.h"
#include "engine/HorseRace.h"
#include "engine/ModelFileTree.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>

#include <cmath>
#include <cstdio>

using namespace studio;

namespace
{

int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
    {
        std::printf("usage: horserace_test <reference dir>\n");
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    if (!QFile::exists(dir + "/e1.csv"))
    {
        std::printf("skip  horse race: run test/exogenous_reference.R first\n");
        return 0;
    }

    HorseRaceDesign d;
    d.data = readDelimited(dir + "/e1.csv");
    d.columns = {0, 1, 2};
    d.horizon = 4;
    const int last = d.data.length() - 1;
    d.firstOrigin = last - 10;
    d.lastOrigin = last - 2;    // its window scores two periods of four
    d.step = 4;
    d.iterations = 400;
    d.burnin = 100;
    std::vector<HorseRaceCandidate> field = standardCandidates(2, false);
    d.candidates = {field[0], field[1]};
    d.benchmark = 0;
    check(horseRaceRefusal(d).isEmpty(), "the design is accepted");
    check(standardCandidates(4, true).size() == 6 && standardCandidates(4, false).size() == 5,
          "the standard field, with a VEC for series in levels");

    const QString folder = dir + "/horserace";
    QDir(folder).removeRecursively();
    const HorseRaceManifest written = writeHorseRace(d, folder);
    check(written.models() == 6 && written.origins.size() == 3, "two models over three origins written");
    const HorseRaceManifest m = readHorseRace(folder);
    check(m.candidates == written.candidates && m.files == written.files && m.origins == written.origins,
          "the manifest reads back");

    // Each window ends at its origin and holds out what follows, up to the horizon.
    {
        const Matrix test = readMatrix(m.files[0][2], QStringLiteral("/data/test/y"));
        const QStringList y = readStringsAttribute(m.files[0][0], QStringLiteral("/data/train/y"),
                                                   QStringLiteral("variables"));
        check(test.cols == 2 && test.rows == 3, "the last window holds out the two periods it can score");
        check(std::abs(test.at(1, 1) - d.data.values(last, 1)) < 1e-12, "... and they are the data's last");
    }

    BatchRunner runner;
    QEventLoop loop;
    int succeeded = 0, failedRuns = 0;
    QObject::connect(&runner, &BatchRunner::failed, [](const QString &file, const QString &message) {
        std::printf("      %s: %s\n", file.toStdString().c_str(), message.toStdString().c_str());
    });
    QObject::connect(&runner, &BatchRunner::finished, [&](int ok, int bad, bool) {
        succeeded = ok;
        failedRuns = bad;
        loop.quit();
    });
    QStringList files;
    for (const auto &c : m.files)
    {
        for (const QString &f : c)
        {
            files << f;
        }
    }
    runner.start(files, 3);
    loop.exec();
    check(succeeded == 6 && failedRuns == 0, "the batch runner runs all six as bayests processes");

    const HorseRaceScores s = scoreHorseRace(m);
    bool finite = true;
    for (std::size_t c = 0; c < 2; ++c)
    {
        finite = finite && s.complete[c] == 3 && s.logScore[c].is_finite();
        // Horizons 1 and 2 seen by three origins, 3 and 4 by two.
        finite = finite && s.observed[c](0, 0) == 3 && s.observed[c](0, 2) == 2 && s.rmse[c].is_finite();
    }
    check(finite, "every window scored: RMSE over the origins that observed each horizon, a log score each");

    // A second run finds every stage in the files and skips them.
    runner.start(files, 3);
    loop.exec();
    const HorseRaceScores again = scoreHorseRace(m);
    check(arma::approx_equal(again.rmse[1], s.rmse[1], "absdiff", 0.0), "a rerun resumes rather than redraws");

    // The scores for test/horserace_reference.R.
    QFile out(folder + "/scores.csv");
    out.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream ts(&out);
    ts << "candidate,measure,variable,horizon,origin,value\n";
    for (std::size_t c = 0; c < 2; ++c)
    {
        for (arma::uword i = 0; i < s.rmse[c].n_rows; ++i)
        {
            for (arma::uword j = 0; j < s.rmse[c].n_cols; ++j)
            {
                ts << c + 1 << ",rmse," << m.variables[static_cast<qsizetype>(i)] << "," << j + 1 << ",,"
                   << QString::number(s.rmse[c](i, j), 'g', 17) << "\n";
            }
        }
        for (arma::uword o = 0; o < s.logScore[c].n_elem; ++o)
        {
            ts << c + 1 << ",logscore,,," << o + 1 << "," << QString::number(s.logScore[c](o), 'g', 17) << "\n";
        }
    }
    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
