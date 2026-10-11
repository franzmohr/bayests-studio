#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <memory>

#include "engine/HorseRace.h"

class QComboBox;
class QLabel;
class QTableWidget;
class QwtPlot;

namespace studio
{

/// The league table of a forecast horse race, the Evaluation stage's second
/// tab: the log predictive score of every model summed over the origins and
/// its difference from the benchmark, the RMSE of each model's point forecasts
/// relative to the benchmark's per horizon, and the cumulative log score
/// difference over the origins -- when a model wins, not only whether.
class HorseRaceView : public QWidget
{
    Q_OBJECT

public:
    explicit HorseRaceView(QWidget *parent = nullptr);

    /// The project folder; a horse race is looked for in its horse-race
    /// subfolder.
    void setFolder(const QString &folder);
    void reload();
    bool hasRace() const { return scores_ != nullptr; }

    /// Writes the tables (LaTeX and CSV) and the figure (PDF) into `folder`.
    bool exportReport(const QString &folder, QString *error = nullptr);

private:
    struct Result
    {
        QString error;
        std::shared_ptr<HorseRaceScores> scores;
    };

    void show();
    void showRmse();

    QString folder_;
    std::shared_ptr<HorseRaceScores> scores_;
    QFutureWatcher<Result> *watcher_;

    QLabel *status_;
    QTableWidget *score_;
    QComboBox *variable_;
    QTableWidget *rmse_;
    QwtPlot *cumulative_;
};

} // namespace studio
