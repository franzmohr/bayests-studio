// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QAbstractItemModel>
#include <QString>
#include <QStringList>

#include <memory>
#include <mutex>
#include <vector>

namespace studio
{

/// The HDF5 library this links is built without thread safety, so no two
/// threads may be inside it at once. Every reader here takes this lock, and
/// an analysis job on a worker thread holds it for the whole job.
std::recursive_mutex &hdf5Mutex();

/// One node of an HDF5 file, read once and held in memory so the file is
/// closed again before a run opens it read-write.
struct H5Node
{
    enum class Kind { Group, Dataset };

    Kind kind = Kind::Group;
    QString name;
    QString path;                         ///< absolute, "/posterior/a/coeffs"
    std::vector<std::size_t> dims;        ///< datasets only, HDF5 order
    QString type;                         ///< datasets only, "float64" etc.
    QStringList attributes;               ///< "name = value"
    H5Node *parent = nullptr;
    std::vector<std::unique_ptr<H5Node>> children;
};

/// A dataset read into memory, row-major as HDF5 stores it: in a BayesTS
/// posterior, one row per quantity and one column per draw.
struct Matrix
{
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> values;

    double at(std::size_t r, std::size_t c) const { return values[r * cols + c]; }
};

/// The string-array attribute `name` of the dataset at `path`, or an empty
/// list when either is absent: the coefficient names the model builder writes
/// on /data/train/z, the variable names on /data/train/y.
QStringList readStringsAttribute(const QString &file, const QString &path, const QString &name);

/// The integer attribute `name` of the group at `path` -- a scalar, or the
/// one-element array R writes -- or `fallback` when either is absent.
int readGroupInt(const QString &file, const QString &path, const QString &name, int fallback);

/// Reads a one- or two-dimensional numeric dataset. Throws on anything else.
Matrix readMatrix(const QString &file, const QString &path);

/// Labels for `count` periods from the first of the estimation sample, by the
/// `tsp` of /data/train/y -- "1960 Q2", "2020-03", "1984" -- or 1, 2, ...
/// where the file has none.
QStringList samplePeriodLabels(const QString &file, int count);

/// The tree of a model file, for the navigator.
class ModelFileTree : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit ModelFileTree(QObject *parent = nullptr);
    ~ModelFileTree() override;

    /// Reloads the tree from disk. Throws if the file cannot be read.
    void load(const QString &file);
    void clear();

    QString file() const { return file_; }
    const H5Node *node(const QModelIndex &index) const;
    const H5Node *find(const QString &path) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QString file_;
    std::unique_ptr<H5Node> root_;
};

} // namespace studio
