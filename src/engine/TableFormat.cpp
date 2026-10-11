#include "engine/TableFormat.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace studio
{

namespace
{

bool flag(const std::vector<std::vector<bool>> &flags, std::size_t r, std::size_t c)
{
    return r < flags.size() && c < flags[r].size() && flags[r][c];
}

/// A number as the views print one: optional sign, digits, decimals, an
/// exponent; a trailing star (the best of a column) and the dash of a missing
/// value count as numeric too, so they do not turn a column left aligned.
bool numeric(const QString &text)
{
    static const QRegularExpression number(
        QStringLiteral(R"(^[-−+]?(\d+([.,]\d*)?|[.,]\d+)([eE][-+]?\d+)?%?(\s*★)?$)"));
    const QString t = text.trimmed();
    return t.isEmpty() || t == QStringLiteral("—") || t == QStringLiteral("–") || number.match(t).hasMatch();
}

QString csvField(const QString &text, QChar separator)
{
    if (text.contains(separator) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n')) ||
        text.contains(QLatin1Char('\r')))
    {
        QString quoted = text;
        quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    return text;
}

QString htmlEscape(const QString &text)
{
    return text.toHtmlEscaped();
}

QString slug(const QString &text)
{
    QString s;
    for (const QChar c : text.toLower())
    {
        if (c.isLetterOrNumber() && c.unicode() < 128)
        {
            s += c;
        }
        else if (!s.endsWith(QLatin1Char('-')) && !s.isEmpty())
        {
            s += QLatin1Char('-');
        }
    }
    while (s.endsWith(QLatin1Char('-')))
    {
        s.chop(1);
    }
    return s.left(40);
}

} // namespace

QString latexText(const QString &text)
{
    static const QHash<QChar, QString> map = {
        {QChar(0x0394), QStringLiteral("\\ensuremath{\\Delta}")},
        {QChar(0x0393), QStringLiteral("\\ensuremath{\\Gamma}")},
        {QChar(0x03A0), QStringLiteral("\\ensuremath{\\Pi}")},
        {QChar(0x03A3), QStringLiteral("\\ensuremath{\\Sigma}")},
        {QChar(0x03A5), QStringLiteral("\\ensuremath{\\Upsilon}")},
        {QChar(0x03A8), QStringLiteral("\\ensuremath{\\Psi}")},
        {QChar(0x03A9), QStringLiteral("\\ensuremath{\\Omega}")},
        {QChar(0x03B1), QStringLiteral("\\ensuremath{\\alpha}")},
        {QChar(0x03B2), QStringLiteral("\\ensuremath{\\beta}")},
        {QChar(0x03B3), QStringLiteral("\\ensuremath{\\gamma}")},
        {QChar(0x03B4), QStringLiteral("\\ensuremath{\\delta}")},
        {QChar(0x03B5), QStringLiteral("\\ensuremath{\\varepsilon}")},
        {QChar(0x03B8), QStringLiteral("\\ensuremath{\\theta}")},
        {QChar(0x03BA), QStringLiteral("\\ensuremath{\\kappa}")},
        {QChar(0x03BB), QStringLiteral("\\ensuremath{\\lambda}")},
        {QChar(0x03BC), QStringLiteral("\\ensuremath{\\mu}")},
        {QChar(0x03C0), QStringLiteral("\\ensuremath{\\pi}")},
        {QChar(0x03C1), QStringLiteral("\\ensuremath{\\rho}")},
        {QChar(0x03C3), QStringLiteral("\\ensuremath{\\sigma}")},
        {QChar(0x03C4), QStringLiteral("\\ensuremath{\\tau}")},
        {QChar(0x03C6), QStringLiteral("\\ensuremath{\\phi}")},
        {QChar(0x03C8), QStringLiteral("\\ensuremath{\\psi}")},
        {QChar(0x03C9), QStringLiteral("\\ensuremath{\\omega}")},
        {QChar(0x2190), QStringLiteral("\\ensuremath{\\leftarrow}")},
        {QChar(0x2192), QStringLiteral("\\ensuremath{\\rightarrow}")},
        {QChar(0x2212), QStringLiteral("\\ensuremath{-}")},
        {QChar(0x2013), QStringLiteral("--")},
        {QChar(0x2014), QStringLiteral("---")},
        {QChar(0x2605), QStringLiteral("\\ensuremath{\\star}")},
        {QChar(0x2264), QStringLiteral("\\ensuremath{\\leq}")},
        {QChar(0x2265), QStringLiteral("\\ensuremath{\\geq}")},
        {QChar(0x00D7), QStringLiteral("\\ensuremath{\\times}")},
        {QChar(0x2032), QStringLiteral("\\ensuremath{'}")},
        {QChar(0x00B7), QStringLiteral("\\ensuremath{\\cdot}")},
        {QChar(0x2080), QStringLiteral("\\ensuremath{_0}")},
        {QChar(0x2081), QStringLiteral("\\ensuremath{_1}")},
        {QChar(0x2082), QStringLiteral("\\ensuremath{_2}")},
        {QChar(0x2093), QStringLiteral("\\ensuremath{_x}")},
        {QChar(0x00B2), QStringLiteral("\\ensuremath{^2}")},
        {QChar(0x207B), QStringLiteral("\\ensuremath{^-}")},
        {QChar(0x00B9), QStringLiteral("\\ensuremath{^1}")},
        {QChar(0x2713), QStringLiteral("yes")},
        {QChar(0x2717), QStringLiteral("no")},
    };
    QString out;
    const QString trimmed = text.trimmed();
    // A number's leading minus is a minus, not a hyphen.
    const bool number = numeric(trimmed) && (trimmed.startsWith(QLatin1Char('-')));
    for (qsizetype i = 0; i < trimmed.size(); ++i)
    {
        const QChar c = trimmed[i];
        switch (c.unicode())
        {
        case '\\': out += QStringLiteral("\\textbackslash{}"); break;
        case '&': out += QStringLiteral("\\&"); break;
        case '%': out += QStringLiteral("\\%"); break;
        case '$': out += QStringLiteral("\\$"); break;
        case '#': out += QStringLiteral("\\#"); break;
        case '_': out += QStringLiteral("\\_"); break;
        case '{': out += QStringLiteral("\\{"); break;
        case '}': out += QStringLiteral("\\}"); break;
        case '~': out += QStringLiteral("\\textasciitilde{}"); break;
        case '^': out += QStringLiteral("\\textasciicircum{}"); break;
        case '<': out += QStringLiteral("\\textless{}"); break;
        case '>': out += QStringLiteral("\\textgreater{}"); break;
        case '-':
            out += (number && i == 0) ? QStringLiteral("\\ensuremath{-}") : QStringLiteral("-");
            break;
        default:
            out += map.value(c, QString(c));
        }
    }
    return out;
}

QString toCsv(const TableContent &table, QChar separator)
{
    const bool rowHeaders = !table.rowHeaders.isEmpty();
    QStringList lines;
    QStringList header;
    if (rowHeaders)
    {
        header << QString();
    }
    for (const QString &h : table.columnHeaders)
    {
        header << csvField(h, separator);
    }
    lines << header.join(separator);
    for (std::size_t r = 0; r < table.cells.size(); ++r)
    {
        QStringList fields;
        if (rowHeaders)
        {
            fields << csvField(table.rowHeaders.value(static_cast<qsizetype>(r)), separator);
        }
        for (const QString &cell : table.cells[r])
        {
            fields << csvField(cell, separator);
        }
        lines << fields.join(separator);
    }
    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

QString toLatex(const TableContent &table, const QString &label)
{
    const bool rowHeaders = !table.rowHeaders.isEmpty();
    const auto columns = static_cast<std::size_t>(table.columnHeaders.size());
    bool anyMuted = false;
    for (const auto &row : table.muted)
    {
        anyMuted = anyMuted || std::any_of(row.begin(), row.end(), [](bool b) { return b; });
    }
    QString spec = rowHeaders ? QStringLiteral("l") : QString();
    for (std::size_t c = 0; c < columns; ++c)
    {
        bool allNumeric = true;
        for (const QStringList &row : table.cells)
        {
            allNumeric = allNumeric && numeric(row.value(static_cast<qsizetype>(c)));
        }
        spec += allNumeric ? QLatin1Char('r') : QLatin1Char('l');
    }
    const auto cellText = [&](std::size_t r, std::size_t c) {
        QString t = latexText(table.cells[r].value(static_cast<qsizetype>(c)));
        if (flag(table.bold, r, c) && !t.isEmpty())
        {
            t = QStringLiteral("\\textbf{%1}").arg(t);
        }
        if (flag(table.muted, r, c) && !t.isEmpty())
        {
            t = QStringLiteral("\\textcolor{gray}{%1}").arg(t);
        }
        return t;
    };

    QStringList out;
    out << QStringLiteral("% Exported from BayesTS Studio. Needs \\usepackage{booktabs}%1.")
               .arg(anyMuted ? QStringLiteral(" and \\usepackage{xcolor}") : QString());
    out << QStringLiteral("\\begin{table}[htbp]");
    out << QStringLiteral("  \\centering");
    if (!table.caption.isEmpty())
    {
        out << QStringLiteral("  \\caption{%1}").arg(latexText(table.caption));
    }
    const QString name = label.isEmpty() ? slug(table.caption) : label;
    if (!name.isEmpty())
    {
        out << QStringLiteral("  \\label{tab:%1}").arg(name);
    }
    out << QStringLiteral("  \\begin{tabular}{%1}").arg(spec);
    out << QStringLiteral("    \\toprule");
    QStringList header;
    if (rowHeaders)
    {
        header << QString();
    }
    for (const QString &h : table.columnHeaders)
    {
        header << latexText(h);
    }
    out << QStringLiteral("    %1 \\\\").arg(header.join(QStringLiteral(" & ")));
    out << QStringLiteral("    \\midrule");
    for (std::size_t r = 0; r < table.cells.size(); ++r)
    {
        if (std::find(table.rulesBefore.begin(), table.rulesBefore.end(), static_cast<int>(r)) !=
            table.rulesBefore.end())
        {
            out << QStringLiteral("    \\midrule");
        }
        QStringList fields;
        if (rowHeaders)
        {
            fields << latexText(table.rowHeaders.value(static_cast<qsizetype>(r)));
        }
        for (std::size_t c = 0; c < columns; ++c)
        {
            fields << cellText(r, c);
        }
        out << QStringLiteral("    %1 \\\\").arg(fields.join(QStringLiteral(" & ")));
    }
    out << QStringLiteral("    \\bottomrule");
    out << QStringLiteral("  \\end{tabular}");
    if (!table.notes.isEmpty())
    {
        out << QStringLiteral("  \\par\\medskip\\footnotesize %1").arg(latexText(table.notes));
    }
    out << QStringLiteral("\\end{table}");
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString toMarkdown(const TableContent &table)
{
    const bool rowHeaders = !table.rowHeaders.isEmpty();
    const auto escape = [](QString t) { return t.replace(QLatin1Char('|'), QStringLiteral("\\|")).trimmed(); };
    QStringList lines;
    if (!table.caption.isEmpty())
    {
        lines << QStringLiteral("**%1**").arg(escape(table.caption)) << QString();
    }
    QStringList header, rule;
    if (rowHeaders)
    {
        header << QString();
        rule << QStringLiteral("---");
    }
    for (std::size_t c = 0; c < static_cast<std::size_t>(table.columnHeaders.size()); ++c)
    {
        header << escape(table.columnHeaders[static_cast<qsizetype>(c)]);
        bool allNumeric = true;
        for (const QStringList &row : table.cells)
        {
            allNumeric = allNumeric && numeric(row.value(static_cast<qsizetype>(c)));
        }
        rule << (allNumeric ? QStringLiteral("---:") : QStringLiteral("---"));
    }
    lines << QStringLiteral("| %1 |").arg(header.join(QStringLiteral(" | ")));
    lines << QStringLiteral("| %1 |").arg(rule.join(QStringLiteral(" | ")));
    for (std::size_t r = 0; r < table.cells.size(); ++r)
    {
        QStringList fields;
        if (rowHeaders)
        {
            fields << escape(table.rowHeaders.value(static_cast<qsizetype>(r)));
        }
        for (std::size_t c = 0; c < table.cells[r].size(); ++c)
        {
            QString t = escape(table.cells[r][static_cast<qsizetype>(c)]);
            if (flag(table.bold, r, c) && !t.isEmpty())
            {
                t = QStringLiteral("**%1**").arg(t);
            }
            fields << t;
        }
        lines << QStringLiteral("| %1 |").arg(fields.join(QStringLiteral(" | ")));
    }
    if (!table.notes.isEmpty())
    {
        lines << QString() << escape(table.notes);
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString toHtml(const TableContent &table)
{
    const bool rowHeaders = !table.rowHeaders.isEmpty();
    QString out = QStringLiteral("<table>");
    if (!table.caption.isEmpty())
    {
        out += QStringLiteral("<caption>%1</caption>").arg(htmlEscape(table.caption));
    }
    out += QStringLiteral("<tr>");
    if (rowHeaders)
    {
        out += QStringLiteral("<th></th>");
    }
    for (const QString &h : table.columnHeaders)
    {
        out += QStringLiteral("<th>%1</th>").arg(htmlEscape(h));
    }
    out += QStringLiteral("</tr>");
    for (std::size_t r = 0; r < table.cells.size(); ++r)
    {
        out += QStringLiteral("<tr>");
        if (rowHeaders)
        {
            out += QStringLiteral("<th>%1</th>").arg(htmlEscape(table.rowHeaders.value(static_cast<qsizetype>(r))));
        }
        for (std::size_t c = 0; c < table.cells[r].size(); ++c)
        {
            QString t = htmlEscape(table.cells[r][static_cast<qsizetype>(c)]);
            if (flag(table.bold, r, c))
            {
                t = QStringLiteral("<b>%1</b>").arg(t);
            }
            if (flag(table.muted, r, c))
            {
                t = QStringLiteral("<span style=\"color:gray\">%1</span>").arg(t);
            }
            out += QStringLiteral("<td>%1</td>").arg(t);
        }
        out += QStringLiteral("</tr>");
    }
    out += QStringLiteral("</table>");
    return out;
}

} // namespace studio
