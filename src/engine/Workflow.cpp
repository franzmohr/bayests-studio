// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/Workflow.h"

#include "engine/HorseRace.h"
#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"
#include "engine/Structural.h"
#include "engine/TableFormat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>

namespace studio
{

// --------------------------------------------------------------------------
// The project

ModelFacts modelFacts(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    ModelFacts f;
    f.file = file;
    const CheckReport r = ModelRunner::check(file, QString());
    f.ok = r.ok;
    f.algorithm = r.algorithm;
    f.estimated = r.hasPosterior;
    f.p = r.p;
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        f.loglik = h5.exist("/posterior/loglik");
        f.scored = h5.exist("/posterior/forecast/loglik");
        f.sign = h5.exist("/model/sign_restrictions/restrictions") && h5.exist("/posterior/q/coeffs");
    }
    catch (const std::exception &)
    {
    }
    return f;
}

const ModelFacts *ProjectState::model(const QString &file) const
{
    for (const ModelFacts &m : models)
    {
        if (QFileInfo(m.file) == QFileInfo(file))
        {
            return &m;
        }
    }
    return nullptr;
}

int ProjectState::estimatedModels() const
{
    return static_cast<int>(std::count_if(models.begin(), models.end(), [](const ModelFacts &m) { return m.estimated; }));
}

int ProjectState::modelsWithLoglik() const
{
    return static_cast<int>(std::count_if(models.begin(), models.end(), [](const ModelFacts &m) { return m.loglik; }));
}

// --------------------------------------------------------------------------
// The workflows

namespace
{

WorkflowStep step(const QString &id, const QString &title, const QString &summary, WorkflowStage stage,
                  const QString &tab, const QString &guidance)
{
    WorkflowStep s;
    s.id = id;
    s.title = title;
    s.summary = summary;
    s.stage = stage;
    s.tab = tab;
    s.guidance = guidance;
    return s;
}

/// The baseline the assistant made, if it is still in the project.
const ModelFacts *baseline(const ProjectState &st)
{
    return st.baselineModels.isEmpty() ? nullptr : st.model(QDir(st.folder).filePath(st.baselineModels.front()));
}

Workflow thesis()
{
    using S = WorkflowStage;
    Workflow w;
    w.id = QStringLiteral("thesis");
    w.title = QObject::tr("Introductory examples");
    w.audience = QObject::tr("For a first empirical project");
    w.summary = QObject::tr("From a research question to the figures and the methods section of a thesis: data, a "
                            "baseline Bayesian VAR, convergence, the lag order, identification, impulse "
                            "responses, robustness and the write-up.");

    WorkflowStep s = step(QStringLiteral("question"), QObject::tr("Research question and data"),
                          QObject::tr("A question a small VAR can answer, and its data"), S::Data, {},
                          QObject::tr(
        "<p>A VAR answers questions about <b>dynamic effects</b>: how do output and prices respond to a monetary "
        "policy shock, to an oil price shock, to a fiscal expansion? Write your question down in one sentence and "
        "name the shock you are after.</p>"
        "<p>Keep the system <b>small</b>: three to five variables, the ones the question needs and the ones the "
        "shock would otherwise pick up. A monetary policy VAR, for instance, has output (real GDP), prices (the "
        "GDP deflator or CPI) and the policy rate (Christiano, Eichenbaum and Evans 1999).</p>"
        "<p>Prepare the data in a spreadsheet: one column per series, a first column with the dates "
        "(<i>1990 Q1</i>, <i>1990-01</i> or <i>1990</i>), and transform as you will use them -- "
        "<b>100 × log</b> of levels, or growth rates. Save it as CSV and import it.</p>"
        "<p>Sources: FRED, the ECB Data Portal, Eurostat, your national central bank and statistical office.</p>"));
    s.done = [](const ProjectState &st) { return st.hasData; };
    s.actions = {{QStringLiteral("import"), QObject::tr("Import Data…"), QObject::tr("A CSV with dates in the first column")}};
    w.steps.push_back(s);

    s = step(QStringLiteral("inspect"), QObject::tr("Look at the data"),
             QObject::tr("Trends, breaks and outliers, and levels or differences"), S::Data, {},
             QObject::tr(
        "<p>Plot every series before modelling it. Look for <b>trends</b> (are the series stationary, or do they "
        "wander?), <b>breaks</b> (a change of monetary regime, the euro, the financial crisis) and "
        "<b>outliers</b> (the pandemic quarters).</p>"
        "<p>Then decide how the series enter:</p><ul>"
        "<li><b>Growth rates or other stationary transformations</b> in a VAR -- the safe choice for a thesis.</li>"
        "<li><b>Log levels</b> in a VAR: fine for impulse responses under a Bayesian prior even if the series are "
        "integrated (Sims, Stock and Watson 1990), and it keeps any long-run relations.</li>"
        "<li><b>Log levels in a VEC</b> when theory gives you a cointegration relation you want to impose.</li></ul>"
        "<p>A sample that spans a break you cannot model is better shortened; the pandemic is usually better "
        "excluded or dummied out.</p>"));
    s.confirm = QObject::tr("I have plotted the series and decided how they enter the model.");
    s.actions = {{QStringLiteral("showData"), QObject::tr("Show the Data"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("baseline"), QObject::tr("Specify the baseline model"),
             QObject::tr("A BVAR with a Minnesota prior, in a justified order"), S::Setup, {},
             QObject::tr(
        "<p>Start from the <b>workhorse</b>: a Bayesian VAR with a constant, the <b>Minnesota prior</b> "
        "(Litterman 1986; Doan, Litterman and Sims 1984) and a Wishart error covariance. The prior shrinks the "
        "many coefficients towards random walks, which is what makes a VAR with several lags estimable on "
        "macroeconomic samples.</p>"
        "<p><b>Lags</b>: four for quarterly data, twelve for monthly, two for annual -- one year of dynamics. "
        "You will compare alternatives in a later step.</p>"
        "<p><b>Order</b> the variables as your identification needs it: under a recursive (Choleski) "
        "identification a variable responds within the period to the shocks of those ordered before it. Slow-"
        "moving variables come first, the policy instrument last: output, prices, then the interest rate.</p>"
        "<p>Fix the <b>seed</b>, so that your results can be reproduced to the last digit.</p>"));
    s.done = [](const ProjectState &st) { return !st.models.empty(); };
    s.actions = {{QStringLiteral("baseline"), QObject::tr("Create the Baseline Model…"),
                  QObject::tr("The recommended specification, with the variables in your order")},
                 {QStringLiteral("builder"), QObject::tr("Use the Full Model Builder…"),
                  QObject::tr("Every option the studio offers")}};
    w.steps.push_back(s);

    s = step(QStringLiteral("estimate"), QObject::tr("Estimate"), QObject::tr("Posterior draws, forecasts and the log likelihood"),
             S::Posterior, {}, QObject::tr(
        "<p>Run the posterior simulation. The studio draws from the posterior with a Gibbs sampler, then computes "
        "the forecasts and the pointwise log likelihood the model comparison needs.</p>"
        "<p>For a Wishart BVAR of this size the run takes seconds; time-varying models take minutes.</p>"));
    s.done = [](const ProjectState &st) {
        const ModelFacts *m = baseline(st);
        return m ? m->estimated : st.estimatedModels() > 0;
    };
    s.actions = {{QStringLiteral("runAll"), QObject::tr("Run All Stages"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("convergence"), QObject::tr("Check convergence"), QObject::tr("Do the draws describe the posterior?"),
             S::Posterior, QObject::tr("Draws and convergence"), QObject::tr(
        "<p>A Gibbs sampler converges to the posterior; check that it has. In <i>Draws and convergence</i> pick a "
        "few coefficients -- own first lags, the coefficients your question is about -- and the error variances.</p>"
        "<ul><li>The <b>trace</b> should look like a hairy caterpillar: no trend, no long excursions.</li>"
        "<li>The running mean should settle early.</li>"
        "<li>The <b>marginal posterior</b> should be smooth.</li></ul>"
        "<p>If they are not, increase the burn-in and the number of draws and estimate again. For a constant BVAR "
        "with a Wishart precision they almost always are; report the number of draws and the burn-in in the "
        "thesis.</p>"));
    s.confirm = QObject::tr("The traces of the coefficients I looked at are stable.");
    s.actions = {{QStringLiteral("showDraws"), QObject::tr("Show the Draws"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("lags"), QObject::tr("Compare lag orders"), QObject::tr("WAIC across lag orders"),
             S::Evaluation, {}, QObject::tr(
        "<p>Let the data speak on the lag order: the assistant creates the baseline with one lag fewer and one "
        "and two more, estimates them, and the <b>Evaluation</b> stage compares them by <b>WAIC</b> "
        "(Watanabe 2010), an estimate of out-of-sample fit; smaller is better.</p>"
        "<p>Keep the baseline unless another order is clearly better -- a difference of a few WAIC points is "
        "noise -- and say in the thesis what you compared.</p>"));
    s.optional = true;
    s.done = [](const ProjectState &st) { return st.events.contains(QStringLiteral("lags")) && st.modelsWithLoglik() >= 2; };
    s.actions = {{QStringLiteral("lagVariants"), QObject::tr("Create and Estimate the Alternatives"),
                  QObject::tr("p − 1, p + 1 and p + 2 lags of the baseline")},
                 {QStringLiteral("showEvaluation"), QObject::tr("Show the Comparison"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("identify"), QObject::tr("Identify the shock"), QObject::tr("Recursive, or by sign restrictions"),
             S::Application, QObject::tr("Structural analysis"), QObject::tr(
        "<p>A VAR's residuals are correlated; to speak of <i>a</i> monetary policy shock you need an "
        "<b>identification</b>. Two standard choices:</p><ul>"
        "<li><b>Recursive</b> (orthogonalised, Choleski): the variable order of the model is a causal order "
        "within the period. Simple, and standard for monetary policy with the rate last.</li>"
        "<li><b>Sign restrictions</b> (Uhlig 2005; Rubio-Ramírez, Waggoner and Zha 2010): say which way the "
        "variables move on impact -- a contractionary policy shock raises the rate and lowers prices -- and keep "
        "only the rotations that agree. Open <i>Sign Restrictions…</i> and fill in the grid.</li></ul>"
        "<p>Justify the scheme in the thesis: it is an assumption, and the results are conditional on it.</p>"));
    s.confirm = QObject::tr("I have chosen an identification and can justify it.");
    s.done = [](const ProjectState &st) { return std::any_of(st.models.begin(), st.models.end(), [](const ModelFacts &m) { return m.sign; }); };
    s.actions = {{QStringLiteral("showStructural"), QObject::tr("Open the Structural Analysis"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("responses"), QObject::tr("Impulse responses and variance decomposition"),
             QObject::tr("The answer to the question"), S::Application, QObject::tr("Structural analysis"), QObject::tr(
        "<p>The <b>impulse responses</b> to the shock of interest answer the question: their sign, size, timing "
        "and persistence. Report the posterior median with 68% and 90% credible bands, as is common since Sims "
        "and Zha (1999), over a horizon of about five years.</p>"
        "<p>The <b>forecast error variance decomposition</b> says how much of each variable's fluctuations the "
        "shock accounts for -- usually little for monetary policy shocks, which is a finding, not a failure.</p>"
        "<p>For the thesis, describe the responses in words, compare them with the literature, and interpret "
        "the bands: a response whose 68% band includes zero is not distinguishable from none.</p>"));
    s.confirm = QObject::tr("I have studied the responses to the shock of interest.");
    s.actions = {{QStringLiteral("showStructural"), QObject::tr("Show the Responses"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("robustness"), QObject::tr("Robustness"), QObject::tr("Do the results survive reasonable changes?"),
             S::Application, QObject::tr("Structural analysis"), QObject::tr(
        "<p>Examiners ask what happens if you change the specification. The assistant estimates the standard "
        "checks as models of their own:</p><ul>"
        "<li>the <b>reverse variable order</b> -- for a recursive identification, how much the ordering drives "
        "the results;</li><li>a <b>looser</b> and a <b>tighter</b> prior;</li>"
        "<li>the <b>later sample</b>, without its first fifth.</li></ul>"
        "<p>Select each in the project list and compare its responses with the baseline's. Report in the thesis "
        "whether the conclusions change, and keep the figures for an appendix.</p>"));
    s.optional = true;
    s.done = [](const ProjectState &st) { return st.events.contains(QStringLiteral("robustness")); };
    s.actions = {{QStringLiteral("robustnessVariants"), QObject::tr("Create and Estimate the Checks"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("export"), QObject::tr("Write it up"), QObject::tr("Figures, tables and a methods paragraph"),
             S::Application, {}, QObject::tr(
        "<p>Export the results for the thesis: every figure of the views as they stand as a PDF, every table as "
        "LaTeX, a <b>draft methods paragraph</b> that states the model, the sample, the prior, the sampler and "
        "the identification, and a <tt>results.tex</tt> that includes them all -- "
        "<tt>\\input{results}</tt> in your document.</p>"
        "<p>Set each view first -- the shock, the horizon, the variable -- since the export takes what they show. "
        "Each figure and table has an Export button of its own for single exports.</p>"
        "<p>Treat the methods paragraph as a draft: check every statement against what you did, and add the "
        "economics.</p>"));
    s.done = [](const ProjectState &st) { return st.events.contains(QStringLiteral("exported")); };
    s.actions = {{QStringLiteral("exportThesis"), QObject::tr("Export Results for the Thesis"), {}}};
    w.steps.push_back(s);
    return w;
}

Workflow horseRace()
{
    using S = WorkflowStage;
    Workflow w;
    w.id = QStringLiteral("horserace");
    w.title = QObject::tr("Forecast horse race");
    w.audience = QObject::tr("For forecasting in a central bank or institute");
    w.summary = QObject::tr("Competing models estimated over expanding windows, forecasting what followed each "
                            "origin, ranked by point and density accuracy against a benchmark.");

    WorkflowStep s = step(QStringLiteral("data"), QObject::tr("Data"), QObject::tr("The series to forecast"),
                          S::Data, {}, QObject::tr(
        "<p>Import the series the models forecast, transformed as they will be forecast -- growth rates for "
        "GDP and prices, levels for interest rates and unemployment -- one column each, dates in the first "
        "column.</p>"
        "<p>A horse race on <b>final</b> data overstates how well any model would have done in real time, as it "
        "forecasts revised figures with revised figures (Croushore and Stark 2001). Say which you use.</p>"));
    s.done = [](const ProjectState &st) { return st.hasData; };
    s.actions = {{QStringLiteral("import"), QObject::tr("Import Data…"), QObject::tr("A CSV with dates in the first column")}};
    w.steps.push_back(s);

    s = step(QStringLiteral("design"), QObject::tr("Design the race"), QObject::tr("Models, origins, horizon and benchmark"),
             S::Evaluation, {}, QObject::tr(
        "<p>A horse race is a <b>pseudo out-of-sample</b> exercise: at every forecast origin each model is "
        "estimated on the data up to that origin only -- an <b>expanding window</b> -- and forecasts what "
        "followed.</p><ul>"
        "<li><b>Origins</b>: the evaluation period, typically the last ten to fifteen years, quarter by quarter. "
        "More origins make the ranking more reliable and the race longer.</li>"
        "<li><b>Horizon</b>: eight quarters is the usual monetary policy horizon.</li>"
        "<li><b>Models</b>: the standard field -- a BVAR with a Minnesota prior as benchmark, the same with "
        "stochastic volatility (Clark 2011), a TVP-VAR (Primiceri 2005), variable selection, a VAR(1) -- and a VEC "
        "for series in levels.</li></ul>"
        "<p>Each model is estimated once per origin, so the race runs models × origins estimations; keep the "
        "chains short enough to finish.</p>"));
    s.done = [](const ProjectState &st) { return st.horseRace; };
    s.actions = {{QStringLiteral("designRace"), QObject::tr("Design the Horse Race…"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("run"), QObject::tr("Run the race"), QObject::tr("Every model at every origin"),
             S::Evaluation, QObject::tr("Horse race"), QObject::tr(
        "<p>Every model is estimated at every origin and forecasts the horizon after it. The runs are "
        "independent, so the studio runs several at once, one per processor core, each with its own seed so "
        "that the race can be reproduced.</p>"
        "<p>A race that is stopped resumes where it stopped: what is in the files is not drawn again.</p>"));
    s.done = [](const ProjectState &st) { return st.horseRace && st.horseRaceModels > 0 && st.horseRaceRun == st.horseRaceModels; };
    s.actions = {{QStringLiteral("runRace"), QObject::tr("Run the Horse Race"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("results"), QObject::tr("Read the league table"), QObject::tr("Point and density accuracy"),
             S::Evaluation, QObject::tr("Horse race"), QObject::tr(
        "<p>Two dimensions of accuracy:</p><ul>"
        "<li><b>Point forecasts</b>: the root mean squared error of the posterior mean forecast, per variable "
        "and horizon, <b>relative to the benchmark</b>. Below one, the model beats it.</li>"
        "<li><b>Density forecasts</b>: the <b>log predictive score</b> of what was realised -- whether the "
        "model's uncertainty was right, not only its centre. Summed over the origins, the difference from the "
        "benchmark is a log Bayes factor of the forecasts (Geweke and Amisano 2010): above zero, the model "
        "wins.</li></ul>"
        "<p>Models with stochastic volatility often win on density without winning on points; that is the "
        "point of them.</p>"));
    s.confirm = QObject::tr("I have read the league table.");
    s.actions = {{QStringLiteral("showRace"), QObject::tr("Show the Results"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("stability"), QObject::tr("When does a model win?"), QObject::tr("The cumulative score over time"),
             S::Evaluation, QObject::tr("Horse race"), QObject::tr(
        "<p>An average hides <i>when</i> a model wins. The cumulative log score difference against the benchmark "
        "rises while the model forecasts better and falls while it does worse: a jump in a crisis says the model "
        "won on one episode, a steady climb that it is better throughout.</p>"
        "<p>Differences of a few log points over a short evaluation sample are within noise; a formal test is "
        "the density forecast comparison of Amisano and Giacomini (2007), the point forecast one of Diebold and "
        "Mariano (1995).</p>"));
    s.optional = true;
    s.confirm = QObject::tr("I have looked at how the ranking evolves.");
    s.actions = {{QStringLiteral("showRace"), QObject::tr("Show the Results"), {}}};
    w.steps.push_back(s);

    s = step(QStringLiteral("report"), QObject::tr("Report"), QObject::tr("The league table and the figure"),
             S::Evaluation, QObject::tr("Horse race"), QObject::tr(
        "<p>Export the league tables as LaTeX or CSV and the cumulative score figure as PDF with the buttons "
        "beside them, or everything at once into the race's <tt>report</tt> folder.</p>"
        "<p>State the design in the note: the origins, the horizon, the window, the data vintage, the draws.</p>"));
    s.done = [](const ProjectState &st) { return st.events.contains(QStringLiteral("raceReport")); };
    s.actions = {{QStringLiteral("exportRace"), QObject::tr("Export the Report"), {}}};
    w.steps.push_back(s);
    return w;
}

} // namespace

const std::vector<Workflow> &workflows()
{
    static const std::vector<Workflow> all{thesis(), horseRace()};
    return all;
}

const Workflow *findWorkflow(const QString &id)
{
    for (const Workflow &w : workflows())
    {
        if (w.id == id)
        {
            return &w;
        }
    }
    return nullptr;
}

bool stepDone(const WorkflowStep &s, const ProjectState &st)
{
    if (s.done && s.done(st))
    {
        return true;
    }
    return !s.confirm.isEmpty() && st.confirmed.contains(s.id);
}

int currentStep(const Workflow &w, const ProjectState &st)
{
    for (std::size_t i = 0; i < w.steps.size(); ++i)
    {
        if (!w.steps[i].optional && !stepDone(w.steps[i], st))
        {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(w.steps.size()) - 1;
}

// --------------------------------------------------------------------------
// The record

AssistantRecord readAssistantRecord(const QString &folder)
{
    AssistantRecord r;
    QFile in(QDir(folder).filePath(QStringLiteral("assistant.json")));
    if (!in.open(QIODevice::ReadOnly))
    {
        return r;
    }
    const QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
    r.workflow = o[QStringLiteral("workflow")].toString();
    r.example = o[QStringLiteral("example")].toString();
    r.dataFile = o[QStringLiteral("dataFile")].toString();
    for (const auto v : o[QStringLiteral("confirmed")].toArray())
    {
        r.confirmed << v.toString();
    }
    for (const auto v : o[QStringLiteral("events")].toArray())
    {
        r.events << v.toString();
    }
    for (const auto v : o[QStringLiteral("models")].toArray())
    {
        r.models << v.toString();
    }
    r.baseline = o[QStringLiteral("baseline")].toObject();
    return r;
}

void writeAssistantRecord(const QString &folder, const AssistantRecord &r)
{
    QJsonObject o;
    o[QStringLiteral("workflow")] = r.workflow;
    o[QStringLiteral("example")] = r.example;
    o[QStringLiteral("dataFile")] = r.dataFile;
    QStringList confirmed(r.confirmed.begin(), r.confirmed.end());
    QStringList events(r.events.begin(), r.events.end());
    confirmed.sort();
    events.sort();
    o[QStringLiteral("confirmed")] = QJsonArray::fromStringList(confirmed);
    o[QStringLiteral("events")] = QJsonArray::fromStringList(events);
    o[QStringLiteral("models")] = QJsonArray::fromStringList(r.models);
    o[QStringLiteral("baseline")] = r.baseline;
    QFile out(QDir(folder).filePath(QStringLiteral("assistant.json")));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        out.write(QJsonDocument(o).toJson());
    }
}

// --------------------------------------------------------------------------
// Specifications

QJsonObject specToJson(const VarBuilderSpec &s)
{
    QJsonObject o;
    QStringList columns, exogenous;
    for (const int c : s.columns)
    {
        columns << s.data.names.value(c);
    }
    for (const int c : s.exogenous)
    {
        exogenous << s.data.names.value(c);
    }
    o[QStringLiteral("family")] = s.vec() ? QStringLiteral("vec") : QStringLiteral("var");
    o[QStringLiteral("columns")] = QJsonArray::fromStringList(columns);
    o[QStringLiteral("exogenous")] = QJsonArray::fromStringList(exogenous);
    o[QStringLiteral("first")] = s.data.periodLabel(s.firstRow);
    o[QStringLiteral("last")] = s.data.periodLabel(s.lastRow < 0 ? s.data.length() - 1 : s.lastRow);
    o[QStringLiteral("p")] = s.p;
    o[QStringLiteral("s")] = s.s;
    o[QStringLiteral("constant")] = s.constant;
    o[QStringLiteral("trend")] = s.trend;
    o[QStringLiteral("rank")] = s.rank;
    if (s.seasonal)
    {
        o[QStringLiteral("seasonal")] = true;
    }
    if (s.vec())
    {
        o[QStringLiteral("cointShrinkage")] = s.cointShrinkage;
        o[QStringLiteral("cointPTauInv")] = s.cointPTauInv;
        if (s.cointScaleFromDifferences)
        {
            o[QStringLiteral("cointScaleFromDifferences")] = true;
        }
    }
    o[QStringLiteral("timeVarying")] = s.timeVarying;
    o[QStringLiteral("errors")] = static_cast<int>(s.errors);
    o[QStringLiteral("covar")] = s.covar;
    o[QStringLiteral("minnesota")] = s.minnesota;
    o[QStringLiteral("randomWalkMean")] = s.randomWalkMean;
    if (s.volatilityBreak >= 0)
    {
        // By its period, as the sample is, so it survives a data set re-imported.
        o[QStringLiteral("volatilityBreak")] = s.data.periodLabel(s.volatilityBreak);
        o[QStringLiteral("volatilityScales")] = s.volatilityScales;
        o[QStringLiteral("volatilityShape")] = s.volatilityShape;
        o[QStringLiteral("rhoShape1")] = s.rhoShape1;
        o[QStringLiteral("rhoShape2")] = s.rhoShape2;
    }
    o[QStringLiteral("kappa1")] = s.kappa1;
    o[QStringLiteral("kappa2")] = s.kappa2;
    o[QStringLiteral("kappa4")] = s.kappa4;
    o[QStringLiteral("coefPrecision")] = s.coefPrecision;
    o[QStringLiteral("detPrecision")] = s.detPrecision;
    o[QStringLiteral("wishartDfExtra")] = s.wishartDfExtra;
    o[QStringLiteral("wishartScale")] = s.wishartScale;
    if (s.nIid > 0)
    {
        o[QStringLiteral("nIid")] = s.nIid;
    }
    if (s.lagDecay != 1.0)
    {
        o[QStringLiteral("lagDecay")] = s.lagDecay;
    }
    if (s.minnesotaScale == VarBuilderSpec::MinnesotaScale::Autoregressions)
    {
        o[QStringLiteral("minnesotaScale")] = QStringLiteral("autoregressions");
    }
    if (!s.ownLagMean.empty())
    {
        // By variable, as the columns are, so a reordering keeps each its own.
        QJsonObject means;
        for (std::size_t i = 0; i < s.ownLagMean.size() && i < s.columns.size(); ++i)
        {
            means[s.data.names.value(s.columns[i])] = s.ownLagMean[i];
        }
        o[QStringLiteral("ownLagMean")] = means;
    }
    if (s.minnesotaDetVariance > 0.0)
    {
        o[QStringLiteral("minnesotaDetVariance")] = s.minnesotaDetVariance;
    }
    if (s.wishartScaleFromResiduals)
    {
        o[QStringLiteral("wishartScaleFromResiduals")] = true;
    }
    // The rest of the form, so that a model the builder wrote can be read
    // back into it (readVarModelSpec()); absent keys read as the defaults.
    o[QStringLiteral("restrictConstant")] = s.restrictConstant;
    o[QStringLiteral("restrictTrend")] = s.restrictTrend;
    o[QStringLiteral("cointRho")] = s.cointRho;
    o[QStringLiteral("structural")] = s.structural;
    o[QStringLiteral("selection")] = static_cast<int>(s.selection);
    o[QStringLiteral("selectDeterministic")] = s.selectDeterministic;
    o[QStringLiteral("selectCovariances")] = s.selectCovariances;
    o[QStringLiteral("inclusionPrior")] = s.inclusionPrior;
    o[QStringLiteral("ssvsTau0")] = s.ssvsTau0;
    o[QStringLiteral("ssvsTau1")] = s.ssvsTau1;
    o[QStringLiteral("covarPrecision")] = s.covarPrecision;
    o[QStringLiteral("nonCentred")] = s.nonCentred;
    o[QStringLiteral("stateShape")] = s.stateShape;
    o[QStringLiteral("stateRate")] = s.stateRate;
    o[QStringLiteral("stateRateDet")] = s.stateRateDet;
    o[QStringLiteral("omegaV")] = s.omegaV;
    o[QStringLiteral("gammaShape")] = s.gammaShape;
    o[QStringLiteral("gammaRate")] = s.gammaRate;
    o[QStringLiteral("svMu")] = s.svMu;
    o[QStringLiteral("svPrecision")] = s.svPrecision;
    o[QStringLiteral("svShape")] = s.svShape;
    o[QStringLiteral("svRate")] = s.svRate;
    o[QStringLiteral("svStateVariance")] = s.svStateVariance;
    o[QStringLiteral("svOffset")] = s.svOffset;
    o[QStringLiteral("svOmegaV")] = s.svOmegaV;
    o[QStringLiteral("iterations")] = s.iterations;
    o[QStringLiteral("burnin")] = s.burnin;
    o[QStringLiteral("thin")] = s.thin;
    o[QStringLiteral("seed")] = s.seed ? static_cast<double>(*s.seed) : -1.0;
    o[QStringLiteral("horizon")] = s.horizon;
    o[QStringLiteral("holdout")] = s.holdout;
    return o;
}

std::optional<VarBuilderSpec> specFromJson(const QJsonObject &o, const DataSet &data)
{
    if (o.isEmpty() || data.empty())
    {
        return std::nullopt;
    }
    VarBuilderSpec s;
    s.data = data;
    const auto index = [&](const QString &name) { return static_cast<int>(data.names.indexOf(name)); };
    for (const auto v : o[QStringLiteral("columns")].toArray())
    {
        const int i = index(v.toString());
        if (i < 0)
        {
            return std::nullopt;   // a different data set
        }
        s.columns.push_back(i);
    }
    for (const auto v : o[QStringLiteral("exogenous")].toArray())
    {
        const int i = index(v.toString());
        if (i < 0)
        {
            return std::nullopt;
        }
        s.exogenous.push_back(i);
    }
    const auto row = [&](const QString &label, int fallback) {
        for (int r = 0; r < data.length(); ++r)
        {
            if (data.periodLabel(r) == label)
            {
                return r;
            }
        }
        return fallback;
    };
    s.firstRow = row(o[QStringLiteral("first")].toString(), 0);
    s.lastRow = row(o[QStringLiteral("last")].toString(), data.length() - 1);
    s.family = o[QStringLiteral("family")].toString() == QLatin1String("vec") ? VarBuilderSpec::Family::Vec
                                                                             : VarBuilderSpec::Family::Var;
    s.p = o[QStringLiteral("p")].toInt(2);
    s.s = o[QStringLiteral("s")].toInt(1);
    s.constant = o[QStringLiteral("constant")].toBool(true);
    s.trend = o[QStringLiteral("trend")].toBool(false);
    s.rank = o[QStringLiteral("rank")].toInt(1);
    s.seasonal = o[QStringLiteral("seasonal")].toBool(false);
    s.cointShrinkage = o[QStringLiteral("cointShrinkage")].toDouble(0.1);
    s.cointPTauInv = o[QStringLiteral("cointPTauInv")].toDouble(1.0);
    s.cointScaleFromDifferences = o[QStringLiteral("cointScaleFromDifferences")].toBool(false);
    s.timeVarying = o[QStringLiteral("timeVarying")].toBool(false);
    s.errors = static_cast<VarBuilderSpec::Errors>(o[QStringLiteral("errors")].toInt(0));
    s.covar = o[QStringLiteral("covar")].toBool(false);
    s.minnesota = o[QStringLiteral("minnesota")].toBool(false);
    s.randomWalkMean = o[QStringLiteral("randomWalkMean")].toBool(false);
    if (const QString at = o[QStringLiteral("volatilityBreak")].toString(); !at.isEmpty())
    {
        s.volatilityBreak = static_cast<int>(s.data.periods.indexOf(at));
        s.volatilityScales = o[QStringLiteral("volatilityScales")].toInt(3);
        s.volatilityShape = o[QStringLiteral("volatilityShape")].toDouble(1.0);
        s.rhoShape1 = o[QStringLiteral("rhoShape1")].toDouble(3.0357);
        s.rhoShape2 = o[QStringLiteral("rhoShape2")].toDouble(1.5089);
    }
    s.kappa1 = o[QStringLiteral("kappa1")].toDouble(0.2);
    s.kappa2 = o[QStringLiteral("kappa2")].toDouble(0.5);
    s.kappa4 = o[QStringLiteral("kappa4")].toDouble(100.0);
    s.coefPrecision = o[QStringLiteral("coefPrecision")].toDouble(1.0);
    s.detPrecision = o[QStringLiteral("detPrecision")].toDouble(0.1);
    s.wishartDfExtra = o[QStringLiteral("wishartDfExtra")].toInt(0);
    s.wishartScale = o[QStringLiteral("wishartScale")].toDouble(1.0);
    s.nIid = o[QStringLiteral("nIid")].toInt(0);
    s.lagDecay = o[QStringLiteral("lagDecay")].toDouble(1.0);
    s.minnesotaScale = o[QStringLiteral("minnesotaScale")].toString() == QLatin1String("autoregressions")
                           ? VarBuilderSpec::MinnesotaScale::Autoregressions
                           : VarBuilderSpec::MinnesotaScale::VarResiduals;
    if (const QJsonObject means = o[QStringLiteral("ownLagMean")].toObject(); !means.isEmpty())
    {
        for (const int c : s.columns)
        {
            s.ownLagMean.push_back(means[data.names.value(c)].toDouble(0.0));
        }
    }
    s.minnesotaDetVariance = o[QStringLiteral("minnesotaDetVariance")].toDouble(0.0);
    s.wishartScaleFromResiduals = o[QStringLiteral("wishartScaleFromResiduals")].toBool(false);
    const auto real = [&](const char *key, double &field) { field = o[QLatin1String(key)].toDouble(field); };
    const auto flag = [&](const char *key, bool &field) { field = o[QLatin1String(key)].toBool(field); };
    flag("restrictConstant", s.restrictConstant);
    flag("restrictTrend", s.restrictTrend);
    real("cointRho", s.cointRho);
    flag("structural", s.structural);
    s.selection = static_cast<VarBuilderSpec::Selection>(o[QStringLiteral("selection")].toInt(0));
    flag("selectDeterministic", s.selectDeterministic);
    flag("selectCovariances", s.selectCovariances);
    real("inclusionPrior", s.inclusionPrior);
    real("ssvsTau0", s.ssvsTau0);
    real("ssvsTau1", s.ssvsTau1);
    real("covarPrecision", s.covarPrecision);
    flag("nonCentred", s.nonCentred);
    real("stateShape", s.stateShape);
    real("stateRate", s.stateRate);
    real("stateRateDet", s.stateRateDet);
    real("omegaV", s.omegaV);
    real("gammaShape", s.gammaShape);
    real("gammaRate", s.gammaRate);
    real("svMu", s.svMu);
    real("svPrecision", s.svPrecision);
    real("svShape", s.svShape);
    real("svRate", s.svRate);
    real("svStateVariance", s.svStateVariance);
    real("svOffset", s.svOffset);
    real("svOmegaV", s.svOmegaV);
    s.iterations = o[QStringLiteral("iterations")].toInt(20000);
    s.burnin = o[QStringLiteral("burnin")].toInt(2000);
    s.thin = std::max(1, o[QStringLiteral("thin")].toInt(1));
    const double seed = o[QStringLiteral("seed")].toDouble(-1.0);
    if (seed >= 0)
    {
        s.seed = static_cast<std::uint64_t>(seed);
    }
    s.horizon = o[QStringLiteral("horizon")].toInt(0);
    s.holdout = o[QStringLiteral("holdout")].toInt(0);
    return s;
}

std::vector<std::pair<QString, VarBuilderSpec>> lagVariants(const VarBuilderSpec &b)
{
    std::vector<std::pair<QString, VarBuilderSpec>> out;
    for (const int p : {b.p - 1, b.p + 1, b.p + 2})
    {
        if (p < 1 || p == b.p)
        {
            continue;
        }
        VarBuilderSpec s = b;
        s.p = p;
        out.emplace_back(QStringLiteral("lags-%1").arg(p), s);
    }
    return out;
}

std::vector<std::pair<QString, VarBuilderSpec>> robustnessVariants(const VarBuilderSpec &b)
{
    std::vector<std::pair<QString, VarBuilderSpec>> out;
    // The surprises stay first: their position is what restricts them.
    if (b.columns.size() > static_cast<std::size_t>(b.nIid) + 1)
    {
        VarBuilderSpec s = b;
        std::reverse(s.columns.begin() + b.nIid, s.columns.end());
        if (s.ownLagMean.size() == s.columns.size())
        {
            std::reverse(s.ownLagMean.begin() + b.nIid, s.ownLagMean.end());
        }
        out.emplace_back(QStringLiteral("robust-reverse-order"), s);
    }
    {
        VarBuilderSpec s = b;
        if (s.minnesota)
        {
            s.kappa1 *= 2.0;
        }
        else
        {
            s.coefPrecision /= 4.0;
        }
        out.emplace_back(QStringLiteral("robust-looser-prior"), s);
    }
    {
        VarBuilderSpec s = b;
        if (s.minnesota)
        {
            s.kappa1 /= 2.0;
        }
        else
        {
            s.coefPrecision *= 4.0;
        }
        out.emplace_back(QStringLiteral("robust-tighter-prior"), s);
    }
    {
        VarBuilderSpec s = b;
        const int last = s.lastRow < 0 ? s.data.length() - 1 : s.lastRow;
        s.firstRow += std::max(1, (last - s.firstRow + 1) / 5);
        out.emplace_back(QStringLiteral("robust-later-sample"), s);
    }
    return out;
}

// --------------------------------------------------------------------------
// The methods paragraph

namespace
{

QString period(double t, double frequency)
{
    const int year = static_cast<int>(std::floor(t + 1e-9));
    const int sub = static_cast<int>(std::lround((t - year) * frequency)) + 1;
    if (frequency == 4.0)
    {
        return QStringLiteral("%1Q%2").arg(year).arg(sub);
    }
    if (frequency == 12.0)
    {
        return QStringLiteral("%1M%2").arg(year).arg(sub, 2, 10, QLatin1Char('0'));
    }
    return QString::number(year);
}

QString listing(const QStringList &items)
{
    if (items.size() <= 1)
    {
        return items.value(0);
    }
    return QStringList(items.mid(0, items.size() - 1)).join(QStringLiteral(", ")) + QObject::tr(" and ") + items.back();
}

} // namespace

QString methodsParagraph(const QString &file, const std::optional<VarBuilderSpec> &spec)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const CheckReport r = ModelRunner::check(file, QString());
    const QStringList variables = levelNames(file);
    const bool vec = r.algorithm.startsWith(QLatin1String("Vec"));
    const bool tvp = r.algorithm.contains(QLatin1String("Tvp"));
    const bool sv = r.algorithm.contains(QLatin1String("Stochvol"));
    double start = 1.0, end = 1.0, frequency = 1.0;
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::DataSet y = h5.getDataSet("/data/train/y");
        if (y.hasAttribute("tsp"))
        {
            const auto tsp = y.getAttribute("tsp").read<std::vector<double>>();
            if (tsp.size() == 3)
            {
                start = tsp[0];
                end = tsp[1];
                frequency = tsp[2];
            }
        }
    }
    catch (const std::exception &)
    {
    }
    const QString frequencyWord = frequency == 4.0    ? QObject::tr("quarterly")
                                  : frequency == 12.0 ? QObject::tr("monthly")
                                  : frequency == 1.0  ? QObject::tr("annual")
                                                      : QString();

    QStringList sentences;
    QString model = vec ? QObject::tr("a Bayesian vector error correction model with cointegration rank %1").arg(r.rank)
                        : QObject::tr("a Bayesian vector autoregression (BVAR)");
    if (tvp && sv)
    {
        model += QObject::tr(" with time-varying coefficients and stochastic volatility (Primiceri 2005)");
    }
    else if (tvp)
    {
        model += QObject::tr(" with time-varying coefficients");
    }
    else if (sv)
    {
        model += QObject::tr(" with stochastic volatility");
    }
    QString deterministic;
    if (spec)
    {
        QStringList terms;
        if (spec->constant)
        {
            terms << QObject::tr("a constant");
        }
        if (spec->trend)
        {
            terms << QObject::tr("a linear trend");
        }
        if (spec->seasonal)
        {
            terms << QObject::tr("seasonal dummies");
        }
        deterministic = terms.isEmpty() ? QString() : (terms.size() == 1 ? QObject::tr(" and %1") : QObject::tr(", %1"))
                                                          .arg(listing(terms));
    }
    sentences << QObject::tr("We estimate %1 with %2 of the %3 variables %4%5, on %6data from %7 to %8 "
                             "(%9 observations after the initial lags).")
                     .arg(model)
                     .arg(vec ? QObject::tr("%1 lags in levels (%2 lagged differences)").arg(r.p).arg(std::max(r.p - 1, 0))
                              : QObject::tr("%1 lags").arg(r.p))
                     .arg(variables.size())
                     .arg(listing(variables), deterministic,
                          frequencyWord.isEmpty() ? QString() : frequencyWord + QLatin1Char(' '),
                          period(start, frequency), period(end, frequency))
                     .arg(r.periods);
    if (vec)
    {
        sentences << (spec && !spec->timeVarying
                          ? QObject::tr("The cointegration space has the prior of Koop, León-González and Strachan "
                                        "(2010), with shrinkage %1 and a central location of %2 times the identity%3.")
                                .arg(spec->cointShrinkage)
                                .arg(spec->cointPTauInv)
                                .arg(spec->cointScaleFromDifferences
                                         ? QObject::tr(" on the error correction term scaled by the standard deviation "
                                                       "of each series' first differences")
                                         : QString())
                          : QObject::tr("The cointegration space has the prior of Koop, León-González and Strachan (2010)."));
    }
    if (spec && spec->nIid > 0)
    {
        QStringList surprises;
        for (int i = 0; i < spec->nIid && i < static_cast<int>(variables.size()); ++i)
        {
            surprises << variables.value(i);
        }
        sentences << QObject::tr("%1 %2 high-frequency %3: %4 no coefficients, so that %5 the other variables "
                                 "only through the error covariance (Jarociński and Karadi 2020).")
                         .arg(listing(surprises))
                         .arg(spec->nIid == 1 ? QObject::tr("is a") : QObject::tr("are"))
                         .arg(spec->nIid == 1 ? QObject::tr("surprise") : QObject::tr("surprises"))
                         .arg(spec->nIid == 1 ? QObject::tr("its equation carries") : QObject::tr("their equations carry"))
                         .arg(spec->nIid == 1 ? QObject::tr("it relates to") : QObject::tr("they relate to"));
    }
    // A panel not observed whole: the entries observed are the constraints,
    // the rest drawn with the parameters.
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (h5.exist("/data/train/constraints/value"))
        {
            const auto observed = static_cast<int>(h5.getDataSet("/data/train/constraints/value").getElementCount());
            const int missing = r.periods * static_cast<int>(variables.size()) - observed;
            if (missing > 0)
            {
                sentences << QObject::tr("The %1 values not observed in the sample are drawn in every sweep of the "
                                         "Gibbs sampler from their distribution given the data and the parameters, "
                                         "so that the results integrate over them rather than condition on a "
                                         "guess.")
                                 .arg(missing);
            }
        }
    }
    catch (const std::exception &)
    {
    }
    if (spec && spec->minnesota)
    {
        // Centred on a random walk for every variable, for some, or none.
        QString centre = spec->randomWalkMean ? QObject::tr("a random walk for every variable") : QObject::tr("white noise");
        if (!spec->ownLagMean.empty())
        {
            QStringList walks;
            for (std::size_t i = 0; i < spec->ownLagMean.size() && i < static_cast<std::size_t>(variables.size()); ++i)
            {
                if (spec->ownLagMean[i] != 0.0)
                {
                    walks << variables.value(static_cast<qsizetype>(i));
                }
            }
            centre = walks.isEmpty() ? QObject::tr("white noise")
                     : walks.size() == static_cast<qsizetype>(spec->ownLagMean.size())
                         ? QObject::tr("a random walk for every variable")
                         : QObject::tr("a random walk for %1 and white noise for the others").arg(listing(walks));
        }
        // In words: the paragraph is escaped for LaTeX, where l^-2 would not survive.
        const QString decay = spec->lagDecay == 1.0   ? QObject::tr("inversely with l")
                              : spec->lagDecay == 2.0 ? QObject::tr("inversely with the square of l")
                                                      : QObject::tr("as l to the power -%1").arg(spec->lagDecay);
        const QString deterministic =
            spec->minnesotaDetVariance > 0.0
                ? QObject::tr("a prior variance of %1 on the deterministic terms").arg(spec->minnesotaDetVariance)
                : QObject::tr("a relative tightness of %1 on the deterministic terms").arg(spec->kappa4);
        const QString scales = spec->minnesotaScale == VarBuilderSpec::MinnesotaScale::Autoregressions
                                   ? QObject::tr("of univariate AR(%1) regressions").arg(spec->p)
                                   : QObject::tr("of least squares estimates");
        sentences << QObject::tr("The coefficients have a Minnesota prior (Litterman 1986) centred on %1, with "
                                 "overall tightness %2, cross-variable tightness %3, the prior standard deviation of "
                                 "lag l falling %4, and %5, the prior variances scaled by the residual variances "
                                 "%6.")
                         .arg(centre)
                         .arg(spec->kappa1)
                         .arg(spec->kappa2)
                         .arg(decay, deterministic, scales);
    }
    else if (spec)
    {
        sentences << QObject::tr("The coefficients have independent normal priors centred on zero with precision "
                                 "%1, and %2 on the deterministic terms.")
                         .arg(spec->coefPrecision)
                         .arg(spec->detPrecision);
    }
    if (r.errorSpec == QLatin1String("wishart") || r.algorithm.endsWith(QLatin1String("Wishart")))
    {
        sentences << QObject::tr("The error covariance has an inverse Wishart prior with %1 degrees of freedom%2.")
                         .arg(spec ? QString::number(static_cast<int>(variables.size()) + spec->wishartDfExtra)
                                   : QObject::tr("k"))
                         .arg(spec && spec->wishartScaleFromResiduals
                                  ? QObject::tr(" and a diagonal scale of the same residual variances")
                                  : QString());
    }
    if (spec && spec->breakInSample())
    {
        sentences << QObject::tr("From %1 the covariance of every shock is scaled by s_t^2, as in Lenza and "
                                 "Primiceri (2022): the first %2 scales are free, with Pareto priors of shape %3, and "
                                 "the excess then decays at a rate with a beta(%4, %5) prior.")
                         .arg(spec->data.periodLabel(spec->volatilityBreak))
                         .arg(spec->volatilityScales)
                         .arg(spec->volatilityShape)
                         .arg(spec->rhoShape1)
                         .arg(spec->rhoShape2);
    }
    sentences << QObject::tr("We draw from the posterior with the Gibbs sampler of the BayesTS library, keeping "
                             "%1 draws after a burn-in of %2%3%4.")
                     .arg(spec ? spec->iterations : r.iterations)
                     .arg(spec ? spec->burnin : r.burnin)
                     .arg(r.thin > 1 ? QObject::tr(" and thinning by %1").arg(r.thin) : QString())
                     .arg(spec && spec->seed ? QObject::tr("; the seed %1 makes them reproducible").arg(*spec->seed)
                          : r.seed.isEmpty() ? QString()
                                             : QObject::tr("; the seed %1 makes them reproducible").arg(r.seed));
    // A sign and zero identification is a resample of the posterior by the
    // importance weights of Arias, Rubio-Ramírez and Waggoner (2018).
    bool signZero = false;
    int accepted = 0, effective = 0, candidates = 0;
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (h5.exist("/model/sign_zero_restrictions"))
        {
            signZero = true;
            const HighFive::Group g = h5.getGroup("/model/sign_zero_restrictions");
            const auto value = [&](const char *name) {
                if (!g.hasAttribute(name))
                {
                    return 0;
                }
                const HighFive::Attribute a = g.getAttribute(name);
                return static_cast<int>(a.getSpace().getNumberDimensions() == 0 ? a.read<double>()
                                                                                : a.read<std::vector<double>>().front());
            };
            accepted = value("accepted");
            effective = value("effective_sample_size");
            candidates = value("candidates");
        }
    }
    catch (const std::exception &)
    {
    }
    if (const auto id = readSignIdentification(file); id && signZero)
    {
        QStringList shocks;
        int zeros = 0, signs = 0;
        for (const SignRestriction &x : id->restrictions)
        {
            const QString name = variables.value(x.impulse);
            if (!shocks.contains(name))
            {
                shocks << name;
            }
            (x.sign == 0 ? zeros : signs) += 1;
        }
        sentences << QObject::tr("The shock named after %1 is identified by %2 zero and %3 sign restrictions on the "
                                 "impulse responses, with the importance sampler of Arias, Rubio-Ramírez and "
                                 "Waggoner (2018): %4 of the %5 posterior draws admit a rotation that satisfies the "
                                 "signs, and the posterior is resampled by the importance weights, an effective "
                                 "sample size of %6.")
                         .arg(listing(shocks))
                         .arg(zeros)
                         .arg(signs)
                         .arg(accepted)
                         .arg(candidates)
                         .arg(effective);
    }
    else if (const auto id = readSignIdentification(file))
    {
        QStringList shocks;
        for (const SignRestriction &x : id->restrictions)
        {
            const QString name = variables.value(x.impulse);
            if (!shocks.contains(name))
            {
                shocks << name;
            }
        }
        double threshold = 0.0;
        for (const SignRestriction &x : id->restrictions)
        {
            threshold = std::max(threshold, x.threshold);
        }
        QStringList rotated;
        for (int i = 0; i < id->rotated && i < static_cast<int>(variables.size()); ++i)
        {
            rotated << variables.value(i);
        }
        sentences << QObject::tr("The shocks to %1 are identified by sign restrictions on the impulse responses "
                                 "(Rubio-Ramírez, Waggoner and Zha 2010), with %2 restrictions%3%4; %5 of the %6 "
                                 "posterior draws admit a rotation that satisfies them.")
                         .arg(listing(shocks))
                         .arg(id->restrictions.size())
                         .arg(threshold > 0.0 ? QObject::tr(", each response beyond %1 in size").arg(threshold)
                                              : QString())
                         .arg(rotated.isEmpty() ? QString()
                                                : QObject::tr(", rotating the shocks to %1 alone and keeping the "
                                                              "others recursive")
                                                      .arg(listing(rotated)))
                         .arg(id->accepted)
                         .arg(id->draws);
    }
    else if (!vec || variables.size() > 1)
    {
        sentences << QObject::tr("Structural shocks are identified recursively, by the Cholesky decomposition of the "
                                 "error covariance in the order %1: a variable responds within the period to the "
                                 "shocks of the variables ordered before it.")
                         .arg(variables.join(QStringLiteral(", ")));
    }
    sentences << QObject::tr("Impulse responses are reported as posterior medians with 68\\% and 90\\% credible "
                             "bands.");
    // Escaped as text, with the one percent sign kept escaped.
    QString text = sentences.join(QLatin1Char(' '));
    text.replace(QStringLiteral("68\\% and 90\\%"), QStringLiteral("68 and 90 percent"));
    return latexText(text);
}

} // namespace studio
