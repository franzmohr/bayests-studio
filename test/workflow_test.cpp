// The assistant's workflows: steps that tick themselves off as the project
// fills, the specification it keeps, the variants it makes, and the methods
// paragraph it drafts.
//
//   studio_workflow_test <reference dir>

#include "engine/CoefficientSummary.h"
#include "engine/DataSet.h"
#include "engine/Examples.h"
#include "engine/Structural.h"
#include "engine/Extensions.h"
#include "engine/ModelRunner.h"
#include "engine/Workflow.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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

    // -- Lütkepohl's E1, which the studio ships: the recipe's log differences
    // are bvartools' diff(log(e1)) * 100, and the example estimated as it is
    // specified gives the numbers of the bvartools introduction.
    {
        const WorkflowExample *e1 = findExample(QStringLiteral("luetkepohl2006"));
        check(e1 && isBuiltinData(e1->recipe) && examplesFor(QStringLiteral("thesis")).front() == e1,
              "Lütkepohl (2006): the first thesis example, on data the studio ships");
        const DataSet d = prepareExampleData(e1->recipe, QString());
        const DataSet ref = readDelimited(dir + "/e1.csv");   // bvartools' diff(log(e1)) * 100, to 1982Q4
        check(d.length() == 75 && d.series() == 3 && d.periods.front() == QStringLiteral("1960 Q2") &&
                  d.periods.back() == QStringLiteral("1978 Q4") && d.names == ref.names &&
                  arma::approx_equal(d.values, ref.values.rows(0, 74), "absdiff", 1e-9),
              "... 75 quarters of 100 x log differences, 1960Q2 to 1978Q4, as bvartools has them");

        QTemporaryDir t;
        DataRecipe bad;
        bad.file = t.filePath(QStringLiteral("zero.csv"));
        bad.dateColumn = QStringLiteral("date");
        bad.columns = {{QStringLiteral("x"), QStringLiteral("x")}};
        bad.transform = DataRecipe::Transform::LogDifference;
        {
            QFile f(bad.file);
            f.open(QIODevice::WriteOnly);
            f.write("date,x\n2000-01-01,1\n2000-02-01,0\n");
        }
        bool refused = false;
        try
        {
            prepareExampleData(bad, QString());
        }
        catch (const std::exception &)
        {
            refused = true;
        }
        check(refused, "... and a log difference of a value not positive refused");

        // The baseline as its dialog fills it in.
        VarBuilderSpec s;
        s.data = d;
        s.columns = {0, 1, 2};
        s.p = e1->p;
        s.constant = true;
        s.minnesota = false;
        s.coefPrecision = e1->coefPrecision;
        s.detPrecision = e1->detPrecision;
        s.wishartScale = e1->wishartScale;
        s.wishartDfExtra = e1->wishartDf - 3;
        s.iterations = e1->iterations;
        s.burnin = e1->burnin;
        s.seed = e1->seed;
        s.horizon = 8;
        const QString file = t.filePath(QStringLiteral("e1.h5"));
        check(summarise(s).refusal.isEmpty(), "... its baseline a model the builder accepts");
        writeVarModel(s, file);
        ModelRunner runner;
        QEventLoop loop;
        bool ran = false;
        QObject::connect(&runner, &ModelRunner::runFinished, &loop, [&](bool ok, bool, const QString &error) {
            ran = ok;
            if (!ok)
            {
                std::printf("      %s\n", error.toStdString().c_str());
            }
            loop.quit();
        });
        runner.start(file, QString(), {Stage::Coefficients, Stage::Forecast, Stage::LogLikelihood});
        loop.exec();
        check(ran, "... and estimated");
        if (ran)
        {
            // bvartools: income.l1 and income.l2 in the cons equation 0.228 and
            // 0.357 (posterior sd 0.115 and 0.112); the oir of cons to income
            // 0.494 on impact; income's share of cons's forecast error variance
            // 0.273 on impact and 0.344 at eight quarters.
            const CoefficientSummary sum = coefficientSummary(file);
            const CoefficientBlock &a = sum.blocks.front();
            const auto mean = [&](int eq, const QString &col) {
                const qsizetype c = a.columns.indexOf(col);
                // Panels run regressor by regressor, the equations within each.
                return c < 0 ? NAN
                             : arma::mean(a.panels.at(static_cast<std::size_t>(c * sum.equations.size() + eq)).draws);
            };
            check(std::abs(mean(2, QStringLiteral("income(-1)")) - 0.228) < 0.02 &&
                      std::abs(mean(2, QStringLiteral("income(-2)")) - 0.357) < 0.02,
                  "... the income lags in the consumption equation as bvartools has them");
            ResponseRequest r;
            r.type = ResponseType::Oir;
            r.horizon = 8;
            const auto oir = impulseResponses(file, r, 1);
            arma::vec impact(oir.size());
            for (std::size_t i = 0; i < oir.size(); ++i)
            {
                impact(i) = oir[i](0, 2);
            }
            const auto fevd = varianceDecomposition(file, r, 2);
            arma::vec share0(fevd.size()), share8(fevd.size());
            for (std::size_t i = 0; i < fevd.size(); ++i)
            {
                share0(i) = fevd[i](0, 1);
                share8(i) = fevd[i](8, 1);
            }
            std::printf("      oir impact %.3f, fevd income share %.3f and %.3f\n", arma::median(impact),
                        arma::mean(share0), arma::mean(share8));
            check(std::abs(arma::median(impact) - 0.494) < 0.03 && std::abs(arma::mean(share0) - 0.273) < 0.03 &&
                      std::abs(arma::mean(share8) - 0.344) < 0.03,
                  "... the response of cons to income and its variance share as bvartools has them");
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

    // -- Extensions: every built-in example survives a round trip through its
    // file form; a folder of files loads, a broken one says why and gives
    // nothing, an id of a built-in replaces it, and the template loads clean.
    {
        bool roundTrip = true;
        for (const WorkflowExample &e : builtinExamples())
        {
            const QJsonObject once = exampleToJson(e);
            QStringList warnings;
            const WorkflowExample back = exampleFromJson(once, QStringLiteral("/somewhere"), &warnings);
            roundTrip = roundTrip && exampleToJson(back) == once && warnings.isEmpty() &&
                        back.candidates == e.candidates && back.benchmark == e.benchmark &&
                        back.restrictions.size() == e.restrictions.size() && back.notes == e.notes;
            if (!roundTrip)
            {
                // Both forms beside each other, to diff.
                for (const auto &[name, json] : {std::pair{"once", once}, std::pair{"back", exampleToJson(back)}})
                {
                    QFile f(QDir::temp().filePath(QStringLiteral("roundtrip-%1.json").arg(QLatin1String(name))));
                    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                    f.write(QJsonDocument(json).toJson());
                }
                std::printf("      round trip of %s: %s (both forms in %s)\n", e.id.toStdString().c_str(),
                            warnings.join(QStringLiteral("; ")).toStdString().c_str(),
                            QDir::tempPath().toStdString().c_str());
                break;
            }
        }
        check(roundTrip, "every built-in example survives its file form, race candidates by key");

        QTemporaryDir t;
        const QDir user(t.filePath(QStringLiteral("extensions")));
        QDir().mkpath(user.path());
        const QDir repro(t.filePath(QStringLiteral("reproductions")));
        QDir().mkpath(repro.filePath(QStringLiteral("2020-someone")));
        const auto write = [](const QString &path, const QByteArray &text) {
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(text);
        };
        // A race on data beside the file, with a typo, and a palette.
        write(repro.filePath(QStringLiteral("2020-someone/data.csv")),
              "DATE,a,b\n2000-01-01,1,2\n2000-02-01,2,3\n2000-03-01,3,5\n");
        write(repro.filePath(QStringLiteral("2020-someone/studio-example.json")), R"js({
  "studio-extension": 1,
  "examples": [{
    "id": "someone2020", "workflow": "horserace", "title": "Forecasting: Someone (2020)",
    "data": {"file": "data.csv", "dateColumn": "DATE", "columns": [{"source": "a"}, {"source": "b", "name": "bee"}]},
    "model": {"lags": 2, "iterations": 300, "burnin": 100, "seed": 7},
    "race": {"firstOrigin": "2000-02", "horizon": 1, "candidates": ["minnesota", "one-lag"], "benchmark": "one-lag"},
    "notes": {"design": "Two models.", "nosuchstep": "never shown"},
    "colour": "a typo"
  }],
  "palettes": [{"id": "two", "name": "Two", "colours": ["#000000", "#ff0000"]}]
})js");
        // Broken JSON, and a candidate the field does not have.
        write(user.filePath(QStringLiteral("broken.json")), "{ \"studio-extension\": 1, ");
        write(user.filePath(QStringLiteral("vec.json")), R"js({"studio-extension": 1, "examples": [{
            "id": "v", "workflow": "horserace", "title": "V",
            "race": {"candidates": ["vec"]}}]})js");
        // A built-in replaced.
        write(user.filePath(QStringLiteral("override.json")), R"js({"studio-extension": 1, "examples": [{
            "id": "arw2018", "workflow": "thesis", "title": "Optimism shocks: replaced"}]})js");

        const QStringList files = extensionFiles(user.path(), repro.path());
        check(files.size() == 4, "the user's *.json and each reproduction's studio-example.json found");
        const std::vector<ExtensionReport> r = loadExtensions(files);
        const auto report = [&](const QString &name) -> const ExtensionReport * {
            for (const ExtensionReport &x : r)
            {
                if (x.file.endsWith(name))
                {
                    return &x;
                }
            }
            return nullptr;
        };
        const ExtensionReport *broken = report(QStringLiteral("broken.json"));
        const ExtensionReport *vec = report(QStringLiteral("vec.json"));
        const ExtensionReport *someone = report(QStringLiteral("studio-example.json"));
        check(broken && broken->error.contains(QStringLiteral("not JSON")) && broken->examples.isEmpty(),
              "broken JSON is reported and gives nothing");
        check(vec && vec->error.contains(QStringLiteral("levels")),
              "a VEC in a race on growth rates is refused, saying it needs levels");
        check(someone && someone->error.isEmpty() && someone->examples == QStringList{QStringLiteral("someone2020")} &&
                  someone->palettes == QStringList{QStringLiteral("two")} && someone->warnings.size() == 2,
              "a reproduction's example and palette read, its typo and its unknown step warned about");
        const WorkflowExample *ex = findExample(QStringLiteral("someone2020"));
        check(ex && ex->benchmark == 1 && ex->candidates == std::vector<int>({0, 4}) &&
                  ex->series == QStringList({QStringLiteral("a"), QStringLiteral("bee")}),
              "its candidates by key, the benchmark among them, the series from the columns");
        const DataSet d = ex ? prepareExampleData(ex->recipe, ex->dataRoot) : DataSet{};
        check(d.series() == 2 && d.length() == 3 && d.names.back() == QStringLiteral("bee"),
              "its data read beside its file");
        const WorkflowExample *arw = findExample(QStringLiteral("arw2018"));
        check(arw && arw->title.endsWith(QStringLiteral("replaced")) && !arw->source.isEmpty(),
              "an extension with a built-in's id replaces it");
        check(extensionPalettes().size() == 1, "the palette kept for the views");

        loadExtensions({});
        check(findExample(QStringLiteral("someone2020")) == nullptr && findExample(QStringLiteral("arw2018")) &&
                  findExample(QStringLiteral("arw2018"))->source.isEmpty(),
              "loading none restores the studio's own");

        const QString templ = writeExtensionTemplate(t.filePath(QStringLiteral("template")));
        const QString asJson = t.filePath(QStringLiteral("template/template.json"));
        QFile::copy(templ, asJson);
        const std::vector<ExtensionReport> tr = loadExtensions({asJson});
        QFile readme(t.filePath(QStringLiteral("template/README.md")));
        readme.open(QIODevice::ReadOnly);
        const QString md = QString::fromUtf8(readme.readAll());
        check(tr.size() == 1 && tr[0].error.isEmpty() && tr[0].warnings.isEmpty() && tr[0].examples.size() == 1 &&
                  tr[0].palettes.size() == 1 && md.contains(QStringLiteral("`inspect`")) &&
                  md.contains(QStringLiteral("`stability`")),
              "the template loads clean, and its README lists every step id");
        loadExtensions({});
    }

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
