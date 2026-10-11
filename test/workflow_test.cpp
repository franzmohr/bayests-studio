// The assistant's workflows: steps that tick themselves off as the project
// fills, the specification it keeps, the variants it makes, and the methods
// paragraph it drafts.
//
//   studio_workflow_test <reference dir>

#include "engine/DataSet.h"
#include "engine/Examples.h"
#include "engine/ModelRunner.h"
#include "engine/Workflow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

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

int indexOf(const Workflow &w, const QString &id)
{
    for (std::size_t i = 0; i < w.steps.size(); ++i)
    {
        if (w.steps[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString dir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    // -- The thesis workflow advances as the project fills.
    const Workflow *thesis = findWorkflow(QStringLiteral("thesis"));
    const Workflow *race = findWorkflow(QStringLiteral("horserace"));
    check(thesis && race && workflows().size() == 2, "two workflows: the master thesis and the horse race");
    ProjectState st;
    st.folder = QDir::tempPath();
    check(currentStep(*thesis, st) == 0, "an empty project starts at the research question");
    st.hasData = true;
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("inspect")),
          "with data, the next step is a judgement: looking at it");
    st.confirmed << QStringLiteral("inspect");
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("baseline")), "confirmed, on to the baseline");
    ModelFacts m;
    m.file = QDir(st.folder).filePath(QStringLiteral("baseline.h5"));
    m.ok = true;
    st.models.push_back(m);
    st.baselineModels << QStringLiteral("baseline.h5");
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("estimate")), "a model, then its estimation");
    st.models[0].estimated = true;
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("convergence")),
          "estimated, then convergence -- the optional lag comparison not blocking");
    st.confirmed << QStringLiteral("convergence");
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("identify")), "then the identification");
    st.models[0].sign = true;
    check(currentStep(*thesis, st) == indexOf(*thesis, QStringLiteral("responses")),
          "a sign identification in a file ticks the identification off");
    st.confirmed << QStringLiteral("responses");
    st.events << QStringLiteral("exported");
    check(currentStep(*thesis, st) == static_cast<int>(thesis->steps.size()) - 1, "everything done: the last step");

    // -- The horse race advances with its files.
    ProjectState hr;
    hr.hasData = true;
    check(currentStep(*race, hr) == indexOf(*race, QStringLiteral("design")), "the race starts with its design");
    hr.horseRace = true;
    hr.horseRaceModels = 12;
    hr.horseRaceRun = 5;
    check(currentStep(*race, hr) == indexOf(*race, QStringLiteral("run")), "a design half run is still running");
    hr.horseRaceRun = 12;
    check(currentStep(*race, hr) == indexOf(*race, QStringLiteral("results")), "run, then the league table");

    // -- The worked examples: their data prepared from the reproductions, and
    // the examples the studio can implement accepted by the builder.
    {
        const QString root = defaultReproductionsFolder();
        check(examplesFor(QStringLiteral("thesis")).size() >= 2 && examplesFor(QStringLiteral("horserace")).size() >= 1,
              "worked examples for both workflows");
        if (QDir(root).exists())
        {
            const WorkflowExample *arw = findExample(QStringLiteral("arw2018"));
            const DataSet d = prepareExampleData(arw->recipe, root);
            check(d.length() == 224 && d.series() == 5 && d.periods.front() == QStringLiteral("1955 Q1") &&
                      d.periods.back() == QStringLiteral("2010 Q4") && d.names.front() == QStringLiteral("tfp"),
                  "Arias et al.: 224 quarters of five series, 1955Q1 to 2010Q4");
            check(std::abs(d.values(0, 0) - 100 * 0.21720722) < 1e-9, "... multiplied by 100");
            const WorkflowExample *lp = findExample(QStringLiteral("lp2022race"));
            const DataSet l = prepareExampleData(lp->recipe, root);
            check(l.periods.front() == QStringLiteral("1988-12") && l.series() == 4 && l.values.is_finite(),
                  "Lenza and Primiceri: four monthly series from 1988-12, complete");
            QTemporaryDir t;
            writeDataSet(d, t.filePath(QStringLiteral("arw.csv")));
            const DataSet back = readDelimited(t.filePath(QStringLiteral("arw.csv")));
            check(back.names == d.names && back.periods == d.periods && arma::approx_equal(back.values, d.values, "absdiff", 1e-12),
                  "a prepared data set written and read back");
        }
        else
        {
            std::printf("skip  example data: no reproductions in %s\n", root.toStdString().c_str());
        }
    }

    // -- Every step says what to do.
    bool complete = true;
    for (const Workflow &w : workflows())
    {
        for (const WorkflowStep &s : w.steps)
        {
            complete = complete && !s.title.isEmpty() && s.guidance.size() > 100 && (s.done || !s.confirm.isEmpty());
        }
    }
    check(complete, "every step has guidance and a way to be done");

    if (dir.isEmpty() || !QFile::exists(dir + "/e1.csv"))
    {
        std::printf("skip  specifications and the methods paragraph: no reference data\n");
        std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
        return failures == 0 ? 0 : 1;
    }

    // -- The specification round trip, and the variants.
    const DataSet data = readDelimited(dir + "/e1.csv");
    VarBuilderSpec b;
    b.data = data;
    b.columns = {2, 0, 1};
    b.p = 4;
    b.minnesota = true;
    b.kappa1 = 0.3;
    b.seed = 42;
    b.firstRow = 3;
    b.lastRow = data.length() - 5;
    const auto back = specFromJson(specToJson(b), data);
    check(back && back->columns == b.columns && back->p == 4 && back->minnesota && back->kappa1 == 0.3 &&
              back->seed == b.seed && back->firstRow == 3 && back->lastRow == b.lastRow,
          "a specification survives JSON, columns and sample by name and label");
    DataSet other = data;
    other.names[0] = QStringLiteral("renamed");
    check(!specFromJson(specToJson(b), other), "... and refuses a data set without its variables");
    const auto lags = lagVariants(b);
    check(lags.size() == 3 && lags[0].second.p == 3 && lags[2].second.p == 6, "lag variants p - 1, p + 1, p + 2");
    const auto robust = robustnessVariants(b);
    check(robust.size() == 4 && robust[0].second.columns == std::vector<int>({1, 0, 2}) &&
              robust[1].second.kappa1 == 0.6 && robust[2].second.kappa1 == 0.15 && robust[3].second.firstRow > b.firstRow,
          "robustness variants: reverse order, looser and tighter prior, later sample");
    bool accepted = true;
    for (const auto &v : robust)
    {
        accepted = accepted && summarise(v.second).refusal.isEmpty();
    }
    check(accepted, "... each a model the builder accepts");

    // -- The methods paragraph, from a model the builder writes and from a
    // sign identified one, and that it compiles.
    QTemporaryDir tmp;
    const QString file = tmp.filePath(QStringLiteral("baseline.h5"));
    writeVarModel(b, file);
    const QString text = methodsParagraph(file, b);
    check(text.contains(QStringLiteral("cons, invest and income")) && text.contains(QStringLiteral("Minnesota")) &&
              text.contains(QStringLiteral("quarterly")) && text.contains(QStringLiteral("seed 42")) &&
              text.contains(QStringLiteral("Cholesky")),
          "the methods paragraph names the variables, prior, frequency, seed and identification");
    const QString signText = QFile::exists(dir + "/sign.h5") ? methodsParagraph(dir + "/sign.h5", std::nullopt) : QString();
    check(signText.isEmpty() || signText.contains(QStringLiteral("sign restrictions")),
          "... and a sign identification where the file has one");
    const QString pdflatex = QStandardPaths::findExecutable(QStringLiteral("pdflatex"));
    if (!pdflatex.isEmpty())
    {
        QFile doc(tmp.filePath(QStringLiteral("doc.tex")));
        doc.open(QIODevice::WriteOnly);
        doc.write(QStringLiteral("\\documentclass{article}\\begin{document}%1\n\n%2\\end{document}\n")
                      .arg(text, signText)
                      .toUtf8());
        doc.close();
        QProcess run;
        run.setWorkingDirectory(tmp.path());
        run.start(pdflatex, {QStringLiteral("-interaction=nonstopmode"), QStringLiteral("-halt-on-error"),
                             QStringLiteral("doc.tex")});
        run.waitForFinished(120000);
        check(run.exitCode() == 0, "the methods paragraphs compile");
    }
    std::printf("%s\n", text.toStdString().c_str());
    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
