#pragma once

#include <QWidget>

#include "engine/ModelRunner.h"

class QLabel;
class QListWidget;

namespace studio
{

/// What the open model is and whether BayesTS will run it: the result of
/// `bayests check`, refreshed whenever the file is opened or a run ends.
class OverviewView : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewView(QWidget *parent = nullptr);

    void showReport(const QString &file, const CheckReport &report);
    void clear();

private:
    QLabel *algorithm_;
    QLabel *file_;
    QLabel *status_;
    QListWidget *facts_;
    QListWidget *warnings_;
};

} // namespace studio
