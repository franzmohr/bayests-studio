// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/DataSet.h"

#include <limits>

#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace studio
{

namespace
{

QStringList splitRow(const QString &line, QChar delimiter)
{
    // Quoted fields, as spreadsheets export them: "a, b" is one field.
    QStringList fields;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line[i];
        if (c == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
            {
                field += '"';
                ++i;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else if (c == delimiter && !quoted)
        {
            fields << field.trimmed();
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    fields << field.trimmed();
    return fields;
}

/// An empty cell, NA, NaN or a lone dot: a value not observed, which the
/// data set holds as NaN.
bool isMissing(const QString &text)
{
    const QString t = text.trimmed().remove(QLatin1Char('"'));
    return t.isEmpty() || t.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0 ||
           t.compare(QLatin1String("NaN"), Qt::CaseInsensitive) == 0 || t == QLatin1String(".");
}

bool toNumber(const QString &text, QChar delimiter, double &out)
{
    bool ok = false;
    out = text.toDouble(&ok);
    if (!ok && delimiter != ',')
    {
        // Semicolon-separated files from a German or French locale write
        // decimal commas.
        QString dotted = text;
        dotted.replace(',', '.');
        out = dotted.toDouble(&ok);
    }
    return ok;
}

} // namespace

TimeIndex timeIndex(const DataSet &data, int first, int last)
{
    TimeIndex index;
    index.start = first + 1;
    index.end = last + 1;
    if (data.periods.size() <= last || first > last)
    {
        return index;
    }

    // Year and sub-period of one label, and the frequency its spelling implies.
    static const QRegularExpression quarter(QStringLiteral(R"(^(\d{4})\s*[-_ ]?\s*[Qq]([1-4])$)"));
    static const QRegularExpression month(QStringLiteral(R"(^(\d{4})[-/.M]?(\d{1,2})(?:[-/.]\d{1,2})?$)"));
    static const QRegularExpression year(QStringLiteral(R"(^(\d{4})$)"));
    const auto parse = [&](const QString &label, double &time, double &frequency) {
        const QString text = label.trimmed();
        if (const auto m = quarter.match(text); m.hasMatch())
        {
            frequency = 4.0;
            time = m.captured(1).toDouble() + (m.captured(2).toDouble() - 1.0) / 4.0;
            return true;
        }
        if (const auto m = year.match(text); m.hasMatch())
        {
            frequency = 1.0;
            time = m.captured(1).toDouble();
            return true;
        }
        if (const auto m = month.match(text); m.hasMatch())
        {
            const double mo = m.captured(2).toDouble();
            if (mo < 1 || mo > 12)
            {
                return false;
            }
            frequency = 12.0;
            time = m.captured(1).toDouble() + (mo - 1.0) / 12.0;
            return true;
        }
        return false;
    };

    double t0 = 0.0, f0 = 0.0, t1 = 0.0, f1 = 0.0;
    if (!parse(data.periods[first], t0, f0) || !parse(data.periods[last], t1, f1) || f0 != f1)
    {
        return index;
    }
    // Monthly labels written as quarter-start dates ("1960-04-01", "1960-07-01")
    // are quarterly: the spacing decides, not the spelling.
    if (f0 == 12.0 && last > first)
    {
        const double step = (t1 - t0) / (last - first);
        if (std::abs(step - 0.25) < 1e-9)
        {
            f0 = 4.0;
        }
        else if (std::abs(step - 1.0) < 1e-9)
        {
            f0 = 1.0;
        }
    }
    // The labels must be evenly spaced at that frequency, or they are not an index.
    if (std::abs((t1 - t0) * f0 - (last - first)) > 1e-6)
    {
        return index;
    }
    index.start = t0;
    index.end = t1;
    index.frequency = f0;
    index.known = true;
    return index;
}

DataSet readDelimited(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error("cannot open " + path.toStdString());
    }
    QTextStream in(&file);

    QString header;
    while (!in.atEnd() && header.trimmed().isEmpty())
    {
        header = in.readLine();
    }
    if (header.startsWith(QChar(0xFEFF)))
    {
        header.remove(0, 1);
    }
    QChar delimiter = ',';
    if (header.count(';') > header.count(delimiter))
    {
        delimiter = ';';
    }
    if (header.count('\t') > header.count(delimiter))
    {
        delimiter = '\t';
    }
    QStringList names = splitRow(header, delimiter);
    for (QString &n : names)
    {
        n.remove('"');
    }

    std::vector<QStringList> rows;
    int lineNo = 1;
    while (!in.atEnd())
    {
        const QString line = in.readLine();
        ++lineNo;
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        QStringList fields = splitRow(line, delimiter);
        if (fields.size() != names.size())
        {
            throw std::runtime_error(QStringLiteral("line %1 has %2 fields, the header %3")
                                         .arg(lineNo).arg(fields.size()).arg(names.size()).toStdString());
        }
        rows.push_back(fields);
    }
    if (rows.empty())
    {
        throw std::runtime_error("no data rows below the header");
    }

    // A first column that does not parse as a number everywhere is labels.
    bool labels = false;
    for (const QStringList &row : rows)
    {
        double ignored = 0.0;
        if (!toNumber(row[0], delimiter, ignored))
        {
            labels = true;
            break;
        }
    }

    DataSet data;
    data.source = path;
    const int first = labels ? 1 : 0;
    data.names = names.mid(first);
    if (data.names.isEmpty())
    {
        throw std::runtime_error("no numeric columns");
    }
    data.values.set_size(rows.size(), static_cast<arma::uword>(data.names.size()));
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        if (labels)
        {
            data.periods << rows[r][0];
        }
        for (int c = first; c < names.size(); ++c)
        {
            double v = 0.0;
            if (isMissing(rows[r][c]))
            {
                v = std::numeric_limits<double>::quiet_NaN();
            }
            else if (!toNumber(rows[r][c], delimiter, v))
            {
                throw std::runtime_error(QStringLiteral("line %1, column '%2': '%3' is not a number")
                                             .arg(static_cast<int>(r) + 2)
                                             .arg(names[c], rows[r][c])
                                             .toStdString());
            }
            data.values(r, static_cast<arma::uword>(c - first)) = v;
        }
    }
    return data;
}

} // namespace studio
