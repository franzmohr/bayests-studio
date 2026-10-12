// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QWidget>

#include <vector>

class QLabel;
class QComboBox;
class QTimer;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotIntervalCurve;

namespace studio
{

class QtReporter;

/// The posterior of one time-varying coefficient path, forming while the
/// chain runs: the median and the 68% and 90% bands per period over the kept
/// draws so far, redrawn a few times a second. After the run, or for a file
/// estimated earlier, the same picture is drawn from /posterior/a/coeffs.
class LiveBandsView : public QWidget
{
    Q_OBJECT

public:
    LiveBandsView(QtReporter *reporter, QWidget *parent = nullptr);

    /// Whether the open model has a time-varying path, its periods, and the
    /// coefficients per period when already known (0 when not: a run that has
    /// not made its first kept draw yet).
    void setModel(bool timeVarying, int periods, int coefficients, const QStringList &names = {});

    void runStarted();
    void runFinished();

    /// Draws the bands from a finished /posterior/a/coeffs, stored as one
    /// column per draw of the path vectorised period by period.
    void showStored(const QString &file);

private:
    void poll();
    void redraw();
    void coefficientChanged(int row);

    QtReporter *reporter_;
    QString file_;
    bool timeVarying_ = false;
    int coefficients_ = 0;
    int periods_ = 0;
    bool live_ = false;

    /// paths_[d][t]: kept draw d, period t, of the watched coefficient.
    std::vector<std::vector<double>> paths_;

    QComboBox *row_;
    QStringList names_;
    void fillRows(int count);
    int currentRow() const;
    QLabel *status_;
    QTimer *timer_;
    QwtPlot *plot_;
    QwtPlotIntervalCurve *band90_;
    QwtPlotIntervalCurve *band68_;
    QwtPlotCurve *median_;
};

} // namespace studio
