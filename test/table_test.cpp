// The table exports: CSV that a spreadsheet reads back cell for cell, and
// LaTeX that compiles.
//
//   studio_table_test
//
// The LaTeX is compiled with pdflatex where one is on PATH, and the test says
// so where it is not.

#include "engine/TableFormat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

using namespace studio;

namespace
{

int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

/// A table with everything the views put in one: the builder's coefficient
/// names, Greek letters, subscripts, arrows, a minus sign, a star, LaTeX's
/// special characters, emphasis, a missing value and a margin row.
TableContent sample()
{
    TableContent t;
    t.caption = QStringLiteral("Posterior inclusion probabilities: lagged differences & Ψ (A₀, 5% level)");
    t.notes = QStringLiteral("Bold: p ≥ 0.5. Grey: not subject to selection; ★ the smallest WAIC; σ² = 1.");
    t.columnHeaders = {QStringLiteral("Δincome(-1)"), QStringLiteral("cons ← income"), QStringLiteral("p_WAIC"),
                       QStringLiteral("#, $ and {}"), QStringLiteral("Model, \"quoted\"")};
    t.rowHeaders = {QStringLiteral("invest"), QStringLiteral("Δcons"), QStringLiteral("To others")};
    t.cells = {{QStringLiteral("0.87"), QStringLiteral("-0.12"), QStringLiteral("12.30  ★"), QStringLiteral("—"),
                QStringLiteral("a,b")},
               {QStringLiteral("1"), QStringLiteral("−3.5"), QStringLiteral("4e-05"), QStringLiteral("x_1 ~ y^2"),
                QStringLiteral("line\nbreak")},
               {QStringLiteral("33.2"), QStringLiteral("0.5"), QStringLiteral("1"), QStringLiteral("β′ w"),
                QStringLiteral("100%")}};
    t.bold = {{true, false, false, false, false}, {false, false, false, false, false},
              {false, true, false, false, false}};
    t.muted = {{false, false, false, false, false}, {true, false, false, false, false},
               {false, false, false, false, false}};
    t.rulesBefore = {2};
    return t;
}

/// RFC 4180 read back: the inverse of toCsv().
std::vector<QStringList> parseCsv(const QString &text)
{
    std::vector<QStringList> rows(1);
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar c = text[i];
        if (quoted)
        {
            if (c == QLatin1Char('"') && i + 1 < text.size() && text[i + 1] == QLatin1Char('"'))
            {
                field += c;
                ++i;
            }
            else if (c == QLatin1Char('"'))
            {
                quoted = false;
            }
            else
            {
                field += c;
            }
        }
        else if (c == QLatin1Char('"'))
        {
            quoted = true;
        }
        else if (c == QLatin1Char(','))
        {
            rows.back() << field;
            field.clear();
        }
        else if (c == QLatin1Char('\r'))
        {
        }
        else if (c == QLatin1Char('\n'))
        {
            rows.back() << field;
            field.clear();
            rows.emplace_back();
        }
        else
        {
            field += c;
        }
    }
    if (rows.back().isEmpty())
    {
        rows.pop_back();
    }
    return rows;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const TableContent t = sample();

    // CSV: every cell back as it went in.
    {
        const std::vector<QStringList> rows = parseCsv(toCsv(t));
        bool same = rows.size() == t.cells.size() + 1 && rows[0].size() == t.columnHeaders.size() + 1;
        for (std::size_t r = 0; same && r < t.cells.size(); ++r)
        {
            same = rows[r + 1].value(0) == t.rowHeaders[static_cast<qsizetype>(r)] &&
                   rows[r + 1].mid(1) == t.cells[r];
        }
        same = same && rows[0].mid(1) == t.columnHeaders;
        check(same, "CSV reads back cell for cell: separators, quotes and line breaks quoted");
    }

    // LaTeX: what the escaping must do, then that it compiles.
    const QString latex = toLatex(t, QStringLiteral("inclusion"));
    check(latex.contains(QStringLiteral("\\ensuremath{\\Delta}income(-1)")), "LaTeX: Δ becomes math");
    check(latex.contains(QStringLiteral("\\ensuremath{\\leftarrow}")), "LaTeX: the arrow becomes math");
    check(latex.contains(QStringLiteral("\\textbf{\\ensuremath{-}0.12}")) == false &&
              latex.contains(QStringLiteral("\\textbf{0.5}")),
          "LaTeX: bold where the view is bold, not elsewhere");
    check(latex.contains(QStringLiteral("\\ensuremath{-}0.12")) && latex.contains(QStringLiteral("\\ensuremath{-}3.5")),
          "LaTeX: a number's minus, hyphen or U+2212, is a minus");
    check(latex.contains(QStringLiteral("p\\_WAIC")) && latex.contains(QStringLiteral("\\#, \\$ and \\{\\}")) &&
              latex.contains(QStringLiteral("100\\%")) && latex.contains(QStringLiteral("x\\_1 \\textasciitilde{}")),
          "LaTeX: special characters escaped");
    check(latex.contains(QStringLiteral("\\textcolor{gray}{1}")) && latex.contains(QStringLiteral("xcolor")),
          "LaTeX: muted cells grey, and the package named");
    check(latex.contains(QStringLiteral("\\begin{tabular}{lrrrll}")), "LaTeX: numeric columns right aligned");
    check(latex.count(QStringLiteral("\\midrule")) == 2, "LaTeX: a rule above the margin row");
    check(latex.contains(QStringLiteral("\\label{tab:inclusion}")), "LaTeX: the label");

    const QString pdflatex = QStandardPaths::findExecutable(QStringLiteral("pdflatex"));
    if (pdflatex.isEmpty())
    {
        std::printf("skip  LaTeX compiles: no pdflatex on PATH\n");
    }
    else
    {
        QTemporaryDir dir;
        QFile doc(dir.filePath(QStringLiteral("doc.tex")));
        doc.open(QIODevice::WriteOnly);
        doc.write(QStringLiteral("\\documentclass{article}\n\\usepackage{booktabs}\n\\usepackage{xcolor}\n"
                                 "\\begin{document}\n%1\n%2\\end{document}\n")
                      .arg(latex, toLatex(TableContent{QStringLiteral("No row headers"), {}, {QStringLiteral("a")},
                                                       {}, {{QStringLiteral("1")}}, {}, {}, {}}))
                      .toUtf8());
        doc.close();
        QProcess run;
        run.setWorkingDirectory(dir.path());
        run.start(pdflatex, {QStringLiteral("-interaction=nonstopmode"), QStringLiteral("-halt-on-error"),
                             QStringLiteral("doc.tex")});
        const bool finished = run.waitForFinished(120000);
        const bool ok = finished && run.exitCode() == 0 && QFile::exists(dir.filePath(QStringLiteral("doc.pdf")));
        check(ok, "LaTeX compiles with pdflatex");
        if (!ok)
        {
            std::printf("%s\n", run.readAllStandardOutput().right(2000).constData());
        }
    }

    // Markdown and HTML keep the emphasis.
    check(toMarkdown(t).contains(QStringLiteral("**0.87**")) && toMarkdown(t).contains(QStringLiteral("---:")),
          "Markdown: bold kept, numeric columns right aligned");
    check(toHtml(t).contains(QStringLiteral("<b>0.87</b>")) &&
              toHtml(t).contains(QStringLiteral("<span style=\"color:gray\">1</span>")),
          "HTML: bold and grey kept");

    std::printf(failures == 0 ? "PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
