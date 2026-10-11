#pragma once

#include <QColor>
#include <QDialog>

#include <vector>

class QHBoxLayout;
class QLabel;
class QListWidget;
class QWidget;

namespace studio
{

/// Chooses the palette the plots draw their series from: the studio's own,
/// the five most used of ggplot2, of matplotlib and of the viridis family,
/// or one of the user's own colours, which the dialog edits.
class PaletteDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PaletteDialog(QWidget *parent = nullptr);

    QString chosen() const;
    std::vector<QColor> custom() const { return custom_; }

private:
    void showChosen();
    void rebuildCustom();

    QListWidget *list_;
    QLabel *source_;
    QLabel *preview_;
    QWidget *editor_;
    QHBoxLayout *swatches_;
    std::vector<QColor> custom_;
};

} // namespace studio
