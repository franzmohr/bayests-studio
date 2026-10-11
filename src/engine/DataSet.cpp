#include "engine/DataSet.h"

#include <QFile>
#include <QLocale>
#include <QTextStream>

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
            if (!toNumber(rows[r][c], delimiter, v))
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
