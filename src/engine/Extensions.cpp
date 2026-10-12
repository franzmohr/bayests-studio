#include "engine/Extensions.h"

#include "engine/HorseRace.h"
#include "engine/Workflow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <stdexcept>

namespace studio
{

namespace
{

std::vector<ExtensionReport> &reportStore()
{
    static std::vector<ExtensionReport> store;
    return store;
}

std::vector<ExtensionPalette> &paletteStore()
{
    static std::vector<ExtensionPalette> store;
    return store;
}

[[noreturn]] void refuse(const QString &what)
{
    throw std::runtime_error(what.toStdString());
}

/// The keys of `o` not in `known`, as warnings under `where`.
void unknownKeys(const QJsonObject &o, const QStringList &known, const QString &where, QStringList *warnings)
{
    if (!warnings)
    {
        return;
    }
    for (const QString &k : o.keys())
    {
        if (!known.contains(k))
        {
            *warnings << QObject::tr("%1: unknown key \"%2\", ignored").arg(where, k);
        }
    }
}

QString text(const QJsonObject &o, const QString &key, const QString &where, bool required = false)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
    {
        if (required)
        {
            refuse(QObject::tr("%1: \"%2\" is missing").arg(where, key));
        }
        return {};
    }
    if (!v.isString())
    {
        refuse(QObject::tr("%1: \"%2\" must be a string").arg(where, key));
    }
    return v.toString();
}

double number(const QJsonObject &o, const QString &key, double fallback, const QString &where)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
    {
        return fallback;
    }
    if (!v.isDouble())
    {
        refuse(QObject::tr("%1: \"%2\" must be a number").arg(where, key));
    }
    return v.toDouble();
}

int integer(const QJsonObject &o, const QString &key, int fallback, const QString &where)
{
    const double d = number(o, key, fallback, where);
    if (d != static_cast<double>(static_cast<long long>(d)))
    {
        refuse(QObject::tr("%1: \"%2\" must be a whole number").arg(where, key));
    }
    return static_cast<int>(d);
}

bool flag(const QJsonObject &o, const QString &key, bool fallback, const QString &where)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
    {
        return fallback;
    }
    if (!v.isBool())
    {
        refuse(QObject::tr("%1: \"%2\" must be true or false").arg(where, key));
    }
    return v.toBool();
}

QStringList strings(const QJsonObject &o, const QString &key, const QString &where)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
    {
        return {};
    }
    if (!v.isArray())
    {
        refuse(QObject::tr("%1: \"%2\" must be a list of strings").arg(where, key));
    }
    QStringList out;
    for (const QJsonValue &x : v.toArray())
    {
        if (!x.isString())
        {
            refuse(QObject::tr("%1: \"%2\" must be a list of strings").arg(where, key));
        }
        out << x.toString();
    }
    return out;
}

QJsonObject object(const QJsonObject &o, const QString &key, const QString &where)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
    {
        return {};
    }
    if (!v.isObject())
    {
        refuse(QObject::tr("%1: \"%2\" must be an object").arg(where, key));
    }
    return v.toObject();
}

/// The race candidates' keys, in the order of standardCandidates for this
/// example: the indices an example stores.
QStringList candidateKeys(const WorkflowExample &e)
{
    QStringList keys;
    for (const HorseRaceCandidate &c : standardCandidates(e.p, e.levels, e.volatilityBreak.isEmpty() ? -1 : 0))
    {
        keys << c.key;
    }
    return keys;
}

QString signText(int sign)
{
    return sign > 0 ? QStringLiteral("+") : sign < 0 ? QStringLiteral("-") : QStringLiteral("0");
}

} // namespace

QString userExtensionsFolder()
{
    // A lab or a portable install points every studio at one folder.
    if (const QString env = qEnvironmentVariable("STUDIO_EXTENSIONS"); !env.isEmpty())
    {
        return env;
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("extensions"));
}

QStringList extensionFiles(const QString &userFolder, const QString &reproductions)
{
    QStringList out;
    if (!userFolder.isEmpty())
    {
        const QDir dir(userFolder);
        for (const QString &f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        {
            out << dir.filePath(f);
        }
    }
    if (!reproductions.isEmpty())
    {
        const QDir root(reproductions);
        for (const QString &project : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        {
            const QString f = QDir(root.filePath(project)).filePath(QStringLiteral("studio-example.json"));
            if (QFileInfo::exists(f))
            {
                out << f;
            }
        }
    }
    return out;
}

QStringList defaultExtensionFiles()
{
    const QString reproductions =
        QSettings().value(QStringLiteral("assistant/reproductions"), defaultReproductionsFolder()).toString();
    return extensionFiles(userExtensionsFolder(), reproductions);
}

QJsonObject exampleToJson(const WorkflowExample &e)
{
    QJsonObject o;
    o[QStringLiteral("id")] = e.id;
    o[QStringLiteral("workflow")] = e.workflow;
    o[QStringLiteral("title")] = e.title;
    o[QStringLiteral("reference")] = e.reference;
    o[QStringLiteral("question")] = e.question;
    if (!e.folder.isEmpty())
    {
        o[QStringLiteral("folder")] = e.folder;
    }
    if (!e.supported)
    {
        o[QStringLiteral("supported")] = false;
        o[QStringLiteral("missing")] = e.missing;
    }
    if (!e.recipe.file.isEmpty())
    {
        // Relative to the file's own folder: a built-in's is relative to the
        // reproductions folder, so its project's folder comes off.
        QString file = e.recipe.file;
        if (e.dataRoot.isEmpty() && !e.folder.isEmpty() && file.startsWith(e.folder + QLatin1Char('/')))
        {
            file = file.mid(e.folder.size() + 1);
        }
        QJsonObject d;
        d[QStringLiteral("file")] = file;
        d[QStringLiteral("dateColumn")] = e.recipe.dateColumn;
        d[QStringLiteral("dates")] = e.recipe.dates == DataRecipe::Dates::YearDotQuarter ? QStringLiteral("year.quarter")
                                                                                         : QStringLiteral("iso");
        QJsonArray columns;
        for (const auto &[from, name] : e.recipe.columns)
        {
            columns.append(QJsonObject{{QStringLiteral("source"), from}, {QStringLiteral("name"), name}});
        }
        d[QStringLiteral("columns")] = columns;
        if (e.recipe.scale != 1.0)
        {
            d[QStringLiteral("scale")] = e.recipe.scale;
        }
        if (!e.recipe.first.isEmpty())
        {
            d[QStringLiteral("first")] = e.recipe.first;
        }
        if (!e.recipe.last.isEmpty())
        {
            d[QStringLiteral("last")] = e.recipe.last;
        }
        if (!e.recipe.dropIncomplete)
        {
            d[QStringLiteral("dropIncomplete")] = false;
        }
        o[QStringLiteral("data")] = d;
    }
    // Stated whole, as the reader takes the data's columns where it is absent.
    QStringList series = e.series;
    if (series.isEmpty())
    {
        for (const auto &[from, name] : e.recipe.columns)
        {
            series << name;
        }
    }
    if (!series.isEmpty())
    {
        o[QStringLiteral("series")] = QJsonArray::fromStringList(series);
    }
    QJsonObject m;
    m[QStringLiteral("lags")] = e.p;
    m[QStringLiteral("minnesota")] = e.minnesota;
    m[QStringLiteral("coefPrecision")] = e.coefPrecision;
    m[QStringLiteral("detPrecision")] = e.detPrecision;
    m[QStringLiteral("wishartScale")] = e.wishartScale;
    m[QStringLiteral("iterations")] = e.iterations;
    m[QStringLiteral("burnin")] = e.burnin;
    m[QStringLiteral("seed")] = static_cast<double>(e.seed);
    o[QStringLiteral("model")] = m;
    if (!e.restrictions.empty())
    {
        QJsonArray r;
        for (const ExampleRestriction &x : e.restrictions)
        {
            r.append(QJsonObject{{QStringLiteral("shock"), x.impulse},
                                 {QStringLiteral("response"), x.response},
                                 {QStringLiteral("sign"), signText(x.sign)},
                                 {QStringLiteral("horizon"), x.horizon}});
        }
        o[QStringLiteral("restrictions")] = r;
    }
    if (e.workflow == QLatin1String("horserace"))
    {
        QJsonObject race;
        race[QStringLiteral("variables")] = QJsonArray::fromStringList(e.variables);
        race[QStringLiteral("firstOrigin")] = e.firstOrigin;
        race[QStringLiteral("lastOrigin")] = e.lastOrigin;
        race[QStringLiteral("step")] = e.step;
        race[QStringLiteral("horizon")] = e.horizon;
        race[QStringLiteral("levels")] = e.levels;
        if (!e.volatilityBreak.isEmpty())
        {
            race[QStringLiteral("volatilityBreak")] = e.volatilityBreak;
        }
        const QStringList keys = candidateKeys(e);
        QJsonArray field;
        for (const int i : e.candidates)
        {
            field.append(keys.value(i));
        }
        race[QStringLiteral("candidates")] = field;
        if (!e.candidates.empty())
        {
            race[QStringLiteral("benchmark")] = keys.value(e.candidates.at(static_cast<std::size_t>(
                std::clamp(e.benchmark, 0, static_cast<int>(e.candidates.size()) - 1))));
        }
        o[QStringLiteral("race")] = race;
    }
    if (!e.notes.empty())
    {
        QJsonObject n;
        for (const auto &[step, note] : e.notes)
        {
            n[step] = note;
        }
        o[QStringLiteral("notes")] = n;
    }
    if (!e.findings.isEmpty())
    {
        o[QStringLiteral("findings")] = e.findings;
    }
    return o;
}

WorkflowExample exampleFromJson(const QJsonObject &o, const QString &base, QStringList *warnings)
{
    WorkflowExample e;
    e.id = text(o, QStringLiteral("id"), QObject::tr("example"), true);
    const QString where = QObject::tr("example \"%1\"").arg(e.id);
    unknownKeys(o,
                {QStringLiteral("id"), QStringLiteral("workflow"), QStringLiteral("title"), QStringLiteral("reference"),
                 QStringLiteral("question"), QStringLiteral("folder"), QStringLiteral("supported"),
                 QStringLiteral("missing"), QStringLiteral("data"), QStringLiteral("series"), QStringLiteral("model"),
                 QStringLiteral("restrictions"), QStringLiteral("race"), QStringLiteral("notes"),
                 QStringLiteral("findings")},
                where, warnings);
    e.workflow = text(o, QStringLiteral("workflow"), where, true);
    const Workflow *workflow = findWorkflow(e.workflow);
    if (!workflow)
    {
        refuse(QObject::tr("%1: \"workflow\" must be \"thesis\" or \"horserace\", not \"%2\"").arg(where, e.workflow));
    }
    e.title = text(o, QStringLiteral("title"), where, true);
    e.reference = text(o, QStringLiteral("reference"), where);
    e.question = text(o, QStringLiteral("question"), where);
    e.folder = text(o, QStringLiteral("folder"), where);
    e.supported = flag(o, QStringLiteral("supported"), true, where);
    e.missing = text(o, QStringLiteral("missing"), where);
    e.dataRoot = base;

    if (const QJsonObject d = object(o, QStringLiteral("data"), where); !d.isEmpty())
    {
        const QString at = where + QObject::tr(", data");
        unknownKeys(d,
                    {QStringLiteral("file"), QStringLiteral("dateColumn"), QStringLiteral("dates"),
                     QStringLiteral("columns"), QStringLiteral("scale"), QStringLiteral("first"),
                     QStringLiteral("last"), QStringLiteral("dropIncomplete")},
                    at, warnings);
        e.recipe.file = text(d, QStringLiteral("file"), at, true);
        e.recipe.dateColumn = text(d, QStringLiteral("dateColumn"), at, true);
        const QString dates = text(d, QStringLiteral("dates"), at);
        if (dates.isEmpty() || dates == QLatin1String("iso"))
        {
            e.recipe.dates = DataRecipe::Dates::IsoDate;
        }
        else if (dates == QLatin1String("year.quarter"))
        {
            e.recipe.dates = DataRecipe::Dates::YearDotQuarter;
        }
        else
        {
            refuse(QObject::tr("%1: \"dates\" must be \"iso\" (1947-01-01) or \"year.quarter\" (1955.1), not \"%2\"")
                       .arg(at, dates));
        }
        const QJsonValue columns = d.value(QStringLiteral("columns"));
        if (!columns.isArray() || columns.toArray().isEmpty())
        {
            refuse(QObject::tr("%1: \"columns\" must list the columns, each {\"source\": ..., \"name\": ...}").arg(at));
        }
        for (const QJsonValue &c : columns.toArray())
        {
            const QJsonObject col = c.toObject();
            const QString source = text(col, QStringLiteral("source"), at + QObject::tr(", column"), true);
            const QString name = text(col, QStringLiteral("name"), at + QObject::tr(", column"));
            e.recipe.columns.emplace_back(source, name.isEmpty() ? source : name);
        }
        e.recipe.scale = number(d, QStringLiteral("scale"), 1.0, at);
        e.recipe.first = text(d, QStringLiteral("first"), at);
        e.recipe.last = text(d, QStringLiteral("last"), at);
        e.recipe.dropIncomplete = flag(d, QStringLiteral("dropIncomplete"), true, at);
    }
    e.series = strings(o, QStringLiteral("series"), where);
    if (e.series.isEmpty())
    {
        for (const auto &[from, name] : e.recipe.columns)
        {
            e.series << name;
        }
    }

    const QJsonObject m = object(o, QStringLiteral("model"), where);
    {
        const QString at = where + QObject::tr(", model");
        unknownKeys(m,
                    {QStringLiteral("lags"), QStringLiteral("minnesota"), QStringLiteral("coefPrecision"),
                     QStringLiteral("detPrecision"), QStringLiteral("wishartScale"), QStringLiteral("iterations"),
                     QStringLiteral("burnin"), QStringLiteral("seed")},
                    at, warnings);
        e.p = integer(m, QStringLiteral("lags"), e.p, at);
        e.minnesota = flag(m, QStringLiteral("minnesota"), e.minnesota, at);
        e.coefPrecision = number(m, QStringLiteral("coefPrecision"), e.coefPrecision, at);
        e.detPrecision = number(m, QStringLiteral("detPrecision"), e.detPrecision, at);
        e.wishartScale = number(m, QStringLiteral("wishartScale"), e.wishartScale, at);
        e.iterations = integer(m, QStringLiteral("iterations"), e.iterations, at);
        e.burnin = integer(m, QStringLiteral("burnin"), e.burnin, at);
        const double seed = number(m, QStringLiteral("seed"), static_cast<double>(e.seed), at);
        if (seed < 0 || seed > 2147483647.0)
        {
            refuse(QObject::tr("%1: \"seed\" must lie between 0 and 2147483647").arg(at));
        }
        e.seed = static_cast<std::uint64_t>(seed);
        if (e.p < 1 || e.iterations < 1 || e.burnin < 0)
        {
            refuse(QObject::tr("%1: \"lags\" and \"iterations\" must be positive, \"burnin\" not negative").arg(at));
        }
    }

    for (const QJsonValue &r : o.value(QStringLiteral("restrictions")).toArray())
    {
        const QJsonObject x = r.toObject();
        const QString at = where + QObject::tr(", restriction");
        unknownKeys(x, {QStringLiteral("shock"), QStringLiteral("response"), QStringLiteral("sign"), QStringLiteral("horizon")},
                    at, warnings);
        ExampleRestriction rr;
        rr.impulse = text(x, QStringLiteral("shock"), at, true);
        rr.response = text(x, QStringLiteral("response"), at, true);
        const QString sign = text(x, QStringLiteral("sign"), at, true);
        if (sign != QLatin1String("+") && sign != QLatin1String("-") && sign != QLatin1String("0"))
        {
            refuse(QObject::tr("%1: \"sign\" must be \"+\", \"-\" or \"0\"").arg(at));
        }
        rr.sign = sign == QLatin1String("+") ? 1 : sign == QLatin1String("-") ? -1 : 0;
        rr.horizon = integer(x, QStringLiteral("horizon"), 0, at);
        if (!e.series.isEmpty() && (!e.series.contains(rr.impulse) || !e.series.contains(rr.response)))
        {
            refuse(QObject::tr("%1: \"%2\" or \"%3\" is not one of the series").arg(at, rr.impulse, rr.response));
        }
        e.restrictions.push_back(rr);
    }

    if (e.workflow == QLatin1String("horserace"))
    {
        const QJsonObject race = object(o, QStringLiteral("race"), where);
        const QString at = where + QObject::tr(", race");
        unknownKeys(race,
                    {QStringLiteral("variables"), QStringLiteral("firstOrigin"), QStringLiteral("lastOrigin"),
                     QStringLiteral("step"), QStringLiteral("horizon"), QStringLiteral("levels"),
                     QStringLiteral("volatilityBreak"), QStringLiteral("candidates"), QStringLiteral("benchmark")},
                    at, warnings);
        e.variables = strings(race, QStringLiteral("variables"), at);
        if (e.variables.isEmpty())
        {
            e.variables = e.series;
        }
        e.firstOrigin = text(race, QStringLiteral("firstOrigin"), at);
        e.lastOrigin = text(race, QStringLiteral("lastOrigin"), at);
        e.step = integer(race, QStringLiteral("step"), e.step, at);
        e.horizon = integer(race, QStringLiteral("horizon"), e.horizon, at);
        e.levels = flag(race, QStringLiteral("levels"), e.levels, at);
        e.volatilityBreak = text(race, QStringLiteral("volatilityBreak"), at);
        const QStringList keys = candidateKeys(e);
        const QStringList field = strings(race, QStringLiteral("candidates"), at);
        for (const QString &k : field)
        {
            const int i = static_cast<int>(keys.indexOf(k));
            if (i < 0)
            {
                refuse(QObject::tr("%1: candidate \"%2\" is not one of %3%4")
                           .arg(at, k, keys.join(QStringLiteral(", ")),
                                k == QLatin1String("vec") ? QObject::tr(" (a VEC races for series in levels only)")
                                : k == QLatin1String("break") ? QObject::tr(" (a break needs \"volatilityBreak\")")
                                                              : QString()));
            }
            e.candidates.push_back(i);
        }
        const QString benchmark = text(race, QStringLiteral("benchmark"), at);
        e.benchmark = 0;
        if (!benchmark.isEmpty())
        {
            const auto it = std::find(e.candidates.begin(), e.candidates.end(), static_cast<int>(keys.indexOf(benchmark)));
            if (it == e.candidates.end())
            {
                refuse(QObject::tr("%1: the benchmark \"%2\" must be one of the candidates").arg(at, benchmark));
            }
            e.benchmark = static_cast<int>(it - e.candidates.begin());
        }
    }

    QSet<QString> steps;
    for (const WorkflowStep &s : workflow->steps)
    {
        steps.insert(s.id);
    }
    const QJsonObject notes = object(o, QStringLiteral("notes"), where);
    for (auto it = notes.begin(); it != notes.end(); ++it)
    {
        if (!steps.contains(it.key()) && warnings)
        {
            *warnings << QObject::tr("%1: the %2 workflow has no step \"%3\"; its note is never shown")
                             .arg(where, e.workflow, it.key());
        }
        e.notes[it.key()] = it.value().toString();
    }
    e.findings = text(o, QStringLiteral("findings"), where);
    return e;
}

std::vector<ExtensionReport> loadExtensions(const QStringList &files)
{
    std::vector<ExtensionReport> out;
    std::vector<WorkflowExample> examplesRead;
    std::vector<ExtensionPalette> palettesRead;
    QSet<QString> seen;
    for (const QString &file : files)
    {
        ExtensionReport r;
        r.file = file;
        try
        {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly))
            {
                refuse(QObject::tr("cannot be opened: %1").arg(f.errorString()));
            }
            QJsonParseError parse;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parse);
            if (parse.error != QJsonParseError::NoError)
            {
                refuse(QObject::tr("is not JSON: %1 at character %2").arg(parse.errorString()).arg(parse.offset));
            }
            const QJsonObject top = doc.object();
            const int format = top.value(QStringLiteral("studio-extension")).toInt(0);
            if (format < 1)
            {
                refuse(QObject::tr("is not a studio extension: \"studio-extension\": 1 is missing"));
            }
            if (format > kExtensionFormat)
            {
                refuse(QObject::tr("needs a newer studio (format %1; this one reads %2)").arg(format).arg(kExtensionFormat));
            }
            unknownKeys(top, {QStringLiteral("studio-extension"), QStringLiteral("examples"), QStringLiteral("palettes")},
                        QObject::tr("the file"), &r.warnings);
            const QString base = QFileInfo(file).absolutePath();
            // Read whole before any of it is kept: a file is in or out.
            std::vector<WorkflowExample> mine;
            for (const QJsonValue &v : top.value(QStringLiteral("examples")).toArray())
            {
                WorkflowExample e = exampleFromJson(v.toObject(), base, &r.warnings);
                e.source = file;
                mine.push_back(std::move(e));
            }
            std::vector<ExtensionPalette> myPalettes;
            for (const QJsonValue &v : top.value(QStringLiteral("palettes")).toArray())
            {
                const QJsonObject p = v.toObject();
                if (p.value(QStringLiteral("id")).toString().isEmpty())
                {
                    refuse(QObject::tr("a palette has no \"id\""));
                }
                myPalettes.push_back({file, p});
            }
            for (WorkflowExample &e : mine)
            {
                if (seen.contains(e.id))
                {
                    r.warnings << QObject::tr("example \"%1\" is also in an earlier file, which wins").arg(e.id);
                    continue;
                }
                seen.insert(e.id);
                r.examples << e.id;
                examplesRead.push_back(std::move(e));
            }
            for (ExtensionPalette &p : myPalettes)
            {
                r.palettes << p.json.value(QStringLiteral("id")).toString();
                palettesRead.push_back(std::move(p));
            }
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        out.push_back(r);
    }
    setExtensionExamples(std::move(examplesRead));
    paletteStore() = std::move(palettesRead);
    reportStore() = out;
    return out;
}

const std::vector<ExtensionReport> &lastExtensionReports()
{
    return reportStore();
}

const std::vector<ExtensionPalette> &extensionPalettes()
{
    return paletteStore();
}

QString writeExtensionTemplate(const QString &folder)
{
    QDir().mkpath(folder);
    const WorkflowExample *base = nullptr;
    for (const WorkflowExample &e : builtinExamples())
    {
        if (e.id == QLatin1String("lp2022race"))
        {
            base = &e;
        }
    }
    QJsonObject example = exampleToJson(base ? *base : builtinExamples().front());
    example[QStringLiteral("id")] = QStringLiteral("my-example");
    example[QStringLiteral("title")] = QStringLiteral("Topic: Authors (year)");
    QJsonObject palette{
        {QStringLiteral("id"), QStringLiteral("my-palette")},
        {QStringLiteral("name"), QStringLiteral("My palette")},
        {QStringLiteral("source"), QStringLiteral("Where the colours come from, and their licence")},
        {QStringLiteral("kind"), QStringLiteral("qualitative")},
        {QStringLiteral("colours"), QJsonArray{QStringLiteral("#1b9e77"), QStringLiteral("#d95f02"),
                                               QStringLiteral("#7570b3"), QStringLiteral("#e7298a")}}};
    const QJsonObject top{{QStringLiteral("studio-extension"), kExtensionFormat},
                          {QStringLiteral("examples"), QJsonArray{example}},
                          {QStringLiteral("palettes"), QJsonArray{palette}}};
    const QString path = QDir(folder).filePath(QStringLiteral("template.json.txt"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        refuse(QObject::tr("Cannot write %1: %2").arg(path, out.errorString()));
    }
    out.write(QJsonDocument(top).toJson(QJsonDocument::Indented));

    QFile readme(QDir(folder).filePath(QStringLiteral("README.md")));
    if (readme.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        readme.write(R"md(# BayesTS Studio extensions

Every `*.json` file in this folder is read at start-up and by Help > Extensions >
Reload, and so is `studio-example.json` in any folder of the reproduction
projects. `template.json.txt` is a working example: copy it to a `.json` file
and edit it.

A file is one object:

    { "studio-extension": 1, "examples": [ ... ], "palettes": [ ... ] }

Paths are relative to the file's folder. Unknown keys are reported, not
silently ignored, and a file with an error contributes nothing: Help >
Extensions lists each file, what it gave and what was wrong.

## Examples

A worked example the assistant offers beside its own. One with the id of a
built-in example replaces it.

| Key | Meaning |
| --- | --- |
| `id` | unique, lower case |
| `workflow` | `"thesis"` or `"horserace"` |
| `title` | `"Topic: Authors (year)"`: the topic heads the entry, the authors label its button |
| `reference`, `question`, `findings` | the citation, the question it answers, what the paper finds |
| `supported`, `missing` | `false` and what is missing, to list a paper as not yet guided |
| `data` | `file` (CSV), `dateColumn`, `dates` (`"iso"`: 1947-01-01; `"year.quarter"`: 1955.1), `columns` (each `{"source", "name"}`), and optionally `scale`, `first`, `last`, `dropIncomplete` |
| `series` | the model's variables in their order; the data's columns by default |
| `model` | `lags`, `minnesota`, `coefPrecision`, `detPrecision`, `wishartScale`, `iterations`, `burnin`, `seed` |
| `restrictions` | thesis: each `{"shock", "response", "sign": "+", "-" or "0", "horizon"}` |
| `race` | horse race: `variables`, `firstOrigin`, `lastOrigin`, `step`, `horizon`, `levels`, `volatilityBreak`, `candidates`, `benchmark` |
| `notes` | per step id, what the example does there |

The candidates of a race are `minnesota`, `sv`, `tvp-sv`, `bvs`, `one-lag`,
`vec` (with `"levels": true`) and `break` (with a `volatilityBreak`). The step
ids of each workflow are listed at the end.

## Palettes

| Key | Meaning |
| --- | --- |
| `id`, `name` | unique id, and the name View > Colour Palette lists |
| `family` | the heading it is listed under; `"Extensions"` by default |
| `source` | where the colours come from, and their licence |
| `kind` | `"qualitative"` (cycled) or `"sequential"` (sampled evenly) |
| `colours` | `"#rrggbb"` strings, at least two |
)md");
        // The step ids, from the workflows themselves.
        QString ids = QStringLiteral("\n## Step ids\n\n");
        for (const Workflow &w : workflows())
        {
            QStringList list;
            for (const WorkflowStep &st : w.steps)
            {
                list << QStringLiteral("`%1`").arg(st.id);
            }
            ids += QStringLiteral("- `%1` (%2): %3\n").arg(w.id, w.title, list.join(QStringLiteral(", ")));
        }
        readme.write(ids.toUtf8());
    }
    return path;
}

} // namespace studio
