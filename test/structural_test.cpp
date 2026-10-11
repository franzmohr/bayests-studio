// The studio's impulse responses and variance decompositions against
// bvartools', draw by draw, on model files bvartools estimated and wrote.
//
//   structural_test <dir>
//
// <dir> is what test/structural_reference.R writes. Every response draw has to
// agree with bvartools' to rounding, and the posterior mean decomposition too.

#include "engine/Structural.h"

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cmath>
#include <cstdio>

using namespace studio;

namespace
{

int failures = 0;

/// A CSV of numbers with a header row: rows x columns.
arma::mat readCsv(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error("cannot open " + path.toStdString());
    }
    QTextStream in(&f);
    in.readLine();
    std::vector<std::vector<double>> rows;
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
        {
            continue;
        }
        std::vector<double> row;
        for (const QString &cell : line.split(','))
        {
            row.push_back(cell.toDouble());
        }
        rows.push_back(row);
    }
    arma::mat m(rows.size(), rows.front().size());
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        for (std::size_t c = 0; c < rows[r].size(); ++c)
        {
            m(r, c) = rows[r][c];
        }
    }
    return m;
}

int indexOf(const StructuralSupport &support, const QString &name)
{
    const int i = static_cast<int>(support.variables.indexOf(name));
    if (i < 0)
    {
        throw std::runtime_error("no variable " + name.toStdString());
    }
    return i;
}

void compareIrf(const QString &dir, const QString &model, ResponseType type, const QString &typeName,
                const QString &impulse, const QString &response)
{
    const QString file = dir + "/" + model + ".h5";
    const StructuralSupport support = structuralSupport(file);
    if (!support.ok)
    {
        std::printf("FAIL  %s: %s\n", model.toStdString().c_str(), support.reason.toStdString().c_str());
        ++failures;
        return;
    }
    const arma::mat reference =
        readCsv(QStringLiteral("%1/%2_irf_%3_%4_%5.csv").arg(dir, model, typeName, impulse, response));
    ResponseRequest request;
    request.type = type;
    request.horizon = static_cast<int>(reference.n_cols) - 1;
    request.maxDraws = static_cast<int>(reference.n_rows);
    const auto draws = impulseResponses(file, request, indexOf(support, impulse));
    const auto j = static_cast<arma::uword>(indexOf(support, response));
    double worst = 0.0;
    for (std::size_t d = 0; d < draws.size(); ++d)
    {
        for (arma::uword h = 0; h < reference.n_cols; ++h)
        {
            worst = std::max(worst, std::abs(draws[d](h, j) - reference(d, h)));
        }
    }
    const bool ok = draws.size() == reference.n_rows && worst < 1e-9;
    std::printf("%s  %s %s %s -> %s: %zu draws, max difference %.2e\n", ok ? "ok  " : "FAIL",
                model.toStdString().c_str(), typeName.toStdString().c_str(),
                impulse.toStdString().c_str(), response.toStdString().c_str(), draws.size(), worst);
    failures += ok ? 0 : 1;
}

void compareFevd(const QString &dir, const QString &model, ResponseType type, const QString &typeName,
                 const QString &response)
{
    const QString file = dir + "/" + model + ".h5";
    const StructuralSupport support = structuralSupport(file);
    const arma::mat reference =
        readCsv(QStringLiteral("%1/%2_fevd_%3_%4.csv").arg(dir, model, typeName, response));
    ResponseRequest request;
    request.type = type;
    request.horizon = static_cast<int>(reference.n_rows) - 1;
    request.maxDraws = 1 << 30;
    const auto draws = varianceDecomposition(file, request, indexOf(support, response));
    arma::mat mean(reference.n_rows, reference.n_cols, arma::fill::zeros);
    for (const auto &d : draws)
    {
        mean += d;
    }
    mean /= static_cast<double>(draws.size());
    const double worst = arma::abs(mean - reference).max();
    const bool ok = worst < 1e-9;
    std::printf("%s  %s fevd %s of %s: max difference %.2e\n", ok ? "ok  " : "FAIL",
                model.toStdString().c_str(), typeName.toStdString().c_str(),
                response.toStdString().c_str(), worst);
    failures += ok ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::printf("usage: structural_test <reference dir>\n");
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    try
    {
        compareIrf(dir, "var2", ResponseType::Feir, "feir", "income", "cons");
        compareIrf(dir, "var2", ResponseType::Oir, "oir", "income", "cons");
        compareIrf(dir, "var2", ResponseType::Gir, "gir", "invest", "income");
        compareFevd(dir, "var2", ResponseType::Oir, "oir", "cons");
        compareIrf(dir, "svar", ResponseType::Sir, "sir", "invest", "cons");
        compareIrf(dir, "svar", ResponseType::Sgir, "sgir", "income", "invest");
        compareFevd(dir, "svar", ResponseType::Sir, "sir", "cons");
        compareIrf(dir, "vec", ResponseType::Oir, "oir", "R", "Dp");
        compareFevd(dir, "vec", ResponseType::Oir, "oir", "Dp");
        compareIrf(dir, "tvp", ResponseType::Oir, "oir", "income", "cons");
        compareFevd(dir, "tvp", ResponseType::Oir, "oir", "cons");
    }
    catch (const std::exception &e)
    {
        std::printf("FAIL  %s\n", e.what());
        ++failures;
    }
    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
