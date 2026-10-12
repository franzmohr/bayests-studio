#pragma once

#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <vector>

#include "engine/DataSet.h"
#include "engine/VarBuilder.h"

namespace studio
{

/// How a published paper's data become a data set the studio imports: a
/// file of the paper's replication material, the columns used and their
/// names in the model, a scale, the dates, and the sample.
struct DataRecipe
{
    QString file;                               ///< relative to the reproductions folder; ":/examples/..." the studio's own
    QString dateColumn;
    enum class Dates
    {
        YearDotQuarter,                         ///< "1955.1"
        IsoDate,                                ///< "1947-01-01", monthly
    } dates = Dates::IsoDate;
    std::vector<std::pair<QString, QString>> columns;   ///< source name, name in the model
    /// Applied to every column within the sample, before the scale: log
    /// differences lose the sample's first period.
    enum class Transform
    {
        None,
        LogDifference,                          ///< log x(t) - log x(t-1)
    } transform = Transform::None;
    double scale = 1.0;                         ///< every column multiplied by it, after the transform
    QString first, last;                        ///< the sample, as the converted labels; empty for all
    bool dropIncomplete = true;                 ///< rows with an empty cell in a chosen column
};

/// A restriction of a worked example, by variable name.
struct ExampleRestriction
{
    QString impulse;
    QString response;
    int sign = 1;                               ///< +1, -1, 0
    int horizon = 0;
};

/// A published paper a user can implement step by step with one of the
/// workflows: its data, the specification the reproduction uses, the
/// identification, and what the user should find.
struct WorkflowExample
{
    QString id;
    QString workflow;                           ///< "thesis" or "horserace"
    QString title;
    QString reference;                          ///< the citation
    QString question;
    QString folder;                             ///< the reproduction's folder
    /// A published paper reproduced, listed under "Reproducing papers"; false
    /// for a textbook example, listed with its workflow.
    bool reproduction = true;
    bool supported = true;
    QString missing;                            ///< what the studio would need, when not supported
    DataRecipe recipe;                          ///< recipe.file empty: the user brings the data
    QStringList series;                         ///< the series' names in the model, in their order

    // The thesis workflow: the baseline and the identification.
    int p = 4;
    double coefPrecision = 1.0;
    double detPrecision = 0.1;
    bool minnesota = false;
    double wishartScale = 1.0;
    int wishartDf = 0;                          ///< the Wishart's degrees of freedom; 0: the builder's, k
    int forecastHorizon = 8;                    ///< the baseline's forecast, periods after the sample
    int iterations = 5000;
    int burnin = 1000;
    std::uint64_t seed = 1;
    std::vector<ExampleRestriction> restrictions;

    // The horse race: the design.
    QStringList variables;
    QString firstOrigin, lastOrigin;
    int step = 1;
    int horizon = 8;
    bool levels = false;                        ///< the series are in levels: random walk priors, and a VEC
    QString volatilityBreak;                    ///< the period a volatility break starts, or empty
    std::vector<int> candidates;                ///< indices into standardCandidates(lags, levels, break)
    int benchmark = 0;

    std::map<QString, QString> notes;           ///< per step id: what this example does there
    QString findings;                           ///< what the paper finds, and the reproduction

    // Where it came from: empty for the studio's own, else the extension file
    // and the folder its data recipe is read relative to.
    QString source;
    QString dataRoot;
};

/// The worked examples: the studio's own, then those of the extensions
/// loaded (engine/Extensions.h), one with the id of a built-in replacing it.
/// Pointers stay valid until the extensions are loaded again.
const std::vector<WorkflowExample> &examples();
/// The studio's own examples alone.
const std::vector<WorkflowExample> &builtinExamples();
/// Replaces the extension examples; examples() is rebuilt.
void setExtensionExamples(std::vector<WorkflowExample> extensions);
std::vector<const WorkflowExample *> examplesFor(const QString &workflow);
const WorkflowExample *findExample(const QString &id);

/// The default place of the reproduction projects, overridden by the
/// setting the assistant stores when the user locates them.
QString defaultReproductionsFolder();

/// Whether a recipe reads data the studio ships, from its resources.
bool isBuiltinData(const DataRecipe &recipe);

/// Applies a recipe to the files under `root` (ignored for built-in data).
/// Throws with the reason -- the file missing, a column not there, a log of
/// a value not positive -- where it cannot.
DataSet prepareExampleData(const DataRecipe &recipe, const QString &root);

/// Writes a data set as the CSV the importer reads: a date column first.
void writeDataSet(const DataSet &data, const QString &path);

} // namespace studio
