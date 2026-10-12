#pragma once

#include <QDialog>

class QLabel;
class QTreeWidget;

namespace studio
{

/// Help > Extensions: where the studio looks for extension files, what each
/// file gave and what was wrong with it, a reload, and a template to start
/// one from (engine/Extensions.h).
class ExtensionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExtensionsDialog(QWidget *parent = nullptr);

signals:
    /// The extensions were read again: examples and palettes may have changed.
    void reloaded();

private:
    void reload();
    void refresh();

    QLabel *folders_;
    QTreeWidget *files_;
    QLabel *summary_;
};

} // namespace studio
