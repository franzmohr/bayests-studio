// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/ModelFileTree.h"

#include <QApplication>
#include <QStyle>

#include <highfive/highfive.hpp>

#include <stdexcept>

namespace studio
{

namespace
{

QString typeName(const HighFive::DataType &type)
{
    switch (type.getClass())
    {
    case HighFive::DataTypeClass::Float: return QStringLiteral("float%1").arg(type.getSize() * 8);
    case HighFive::DataTypeClass::Integer: return QStringLiteral("int%1").arg(type.getSize() * 8);
    case HighFive::DataTypeClass::String: return QStringLiteral("string");
    default: return QStringLiteral("other");
    }
}

QString attributeValue(const HighFive::Attribute &attr)
{
    const HighFive::DataType type = attr.getDataType();
    if (attr.getSpace().getElementCount() != 1)
    {
        return QStringLiteral("[%1 values]").arg(attr.getSpace().getElementCount());
    }
    switch (type.getClass())
    {
    case HighFive::DataTypeClass::String:
        return QString::fromStdString(attr.read<std::string>());
    case HighFive::DataTypeClass::Float:
        return QString::number(attr.read<double>(), 'g', 10);
    case HighFive::DataTypeClass::Integer:
        return QString::number(attr.read<long long>());
    case HighFive::DataTypeClass::Enum:
        // h5py, HighFive and R write booleans as an enumeration over int8.
        return attr.read<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    default:
        return QStringLiteral("?");
    }
}

template <typename Object>
void readAttributes(const Object &object, H5Node &node)
{
    for (const std::string &name : object.listAttributeNames())
    {
        QString value;
        try
        {
            value = attributeValue(object.getAttribute(name));
        }
        catch (const std::exception &)
        {
            value = QStringLiteral("?");
        }
        node.attributes << QStringLiteral("%1 = %2").arg(QString::fromStdString(name), value);
    }
}

void readGroup(const HighFive::Group &group, H5Node &node)
{
    readAttributes(group, node);
    for (const std::string &name : group.listObjectNames())
    {
        auto child = std::make_unique<H5Node>();
        child->name = QString::fromStdString(name);
        child->path = (node.path == "/" ? QString("/") : node.path + "/") + child->name;
        child->parent = &node;

        switch (group.getObjectType(name))
        {
        case HighFive::ObjectType::Group:
            child->kind = H5Node::Kind::Group;
            readGroup(group.getGroup(name), *child);
            break;
        case HighFive::ObjectType::Dataset:
        {
            child->kind = H5Node::Kind::Dataset;
            const HighFive::DataSet ds = group.getDataSet(name);
            child->dims = ds.getSpace().getDimensions();
            child->type = typeName(ds.getDataType());
            readAttributes(ds, *child);
            break;
        }
        default:
            continue;
        }
        node.children.push_back(std::move(child));
    }
}

QString shape(const H5Node &node)
{
    QStringList parts;
    for (const std::size_t d : node.dims)
    {
        parts << QString::number(d);
    }
    return QStringLiteral("(%1)").arg(parts.join(QStringLiteral(" × ")));
}

} // namespace

std::recursive_mutex &hdf5Mutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

QStringList readStringsAttribute(const QString &file, const QString &path, const QString &name)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    QStringList out;
    try
    {
        HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (!h5.exist(path.toStdString()))
        {
            return out;
        }
        const HighFive::DataSet ds = h5.getDataSet(path.toStdString());
        if (!ds.hasAttribute(name.toStdString()))
        {
            return out;
        }
        for (const std::string &s : ds.getAttribute(name.toStdString()).read<std::vector<std::string>>())
        {
            out << QString::fromStdString(s);
        }
    }
    catch (const std::exception &)
    {
        out.clear();
    }
    return out;
}

int readGroupInt(const QString &file, const QString &path, const QString &name, int fallback)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (!h5.exist(path.toStdString()))
        {
            return fallback;
        }
        const HighFive::Group g = h5.getGroup(path.toStdString());
        if (!g.hasAttribute(name.toStdString()))
        {
            return fallback;
        }
        const HighFive::Attribute a = g.getAttribute(name.toStdString());
        if (a.getSpace().getElementCount() == 0)
        {
            return fallback;
        }
        // Read in the attribute's own class -- HighFive warns on a conversion --
        // an integer as BayesTS and bayests-r write one, a double as R may.
        const bool integer = a.getDataType().getClass() == HighFive::DataTypeClass::Integer;
        if (a.getSpace().getNumberDimensions() == 0)
        {
            return integer ? static_cast<int>(a.read<long long>()) : static_cast<int>(a.read<double>());
        }
        if (integer)
        {
            const auto v = a.read<std::vector<long long>>();
            return v.empty() ? fallback : static_cast<int>(v.front());
        }
        const auto v = a.read<std::vector<double>>();
        return v.empty() ? fallback : static_cast<int>(v.front());
    }
    catch (const std::exception &)
    {
        return fallback;
    }
}

Matrix readMatrix(const QString &file, const QString &path)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    const HighFive::DataSet ds = h5.getDataSet(path.toStdString());
    const std::vector<std::size_t> dims = ds.getSpace().getDimensions();

    Matrix m;
    if (dims.size() == 1)
    {
        m.rows = 1;
        m.cols = dims[0];
    }
    else if (dims.size() == 2)
    {
        m.rows = dims[0];
        m.cols = dims[1];
    }
    else
    {
        throw std::runtime_error(path.toStdString() + " is not one- or two-dimensional");
    }
    m.values.resize(m.rows * m.cols);
    if (!m.values.empty())
    {
        ds.read_raw(m.values.data());
    }
    return m;
}

ModelFileTree::ModelFileTree(QObject *parent) : QAbstractItemModel(parent) {}
ModelFileTree::~ModelFileTree() = default;

void ModelFileTree::load(const QString &file)
{
    auto root = std::make_unique<H5Node>();
    root->name = QStringLiteral("/");
    root->path = QStringLiteral("/");
    {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        readGroup(h5.getGroup("/"), *root);
    }

    beginResetModel();
    file_ = file;
    root_ = std::move(root);
    endResetModel();
}

void ModelFileTree::clear()
{
    beginResetModel();
    file_.clear();
    root_.reset();
    endResetModel();
}

const H5Node *ModelFileTree::node(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return root_.get();
    }
    return static_cast<const H5Node *>(index.internalPointer());
}

const H5Node *ModelFileTree::find(const QString &path) const
{
    if (!root_)
    {
        return nullptr;
    }
    const H5Node *current = root_.get();
    for (const QString &part : path.split('/', Qt::SkipEmptyParts))
    {
        const H5Node *next = nullptr;
        for (const auto &child : current->children)
        {
            if (child->name == part)
            {
                next = child.get();
                break;
            }
        }
        if (next == nullptr)
        {
            return nullptr;
        }
        current = next;
    }
    return current;
}

QModelIndex ModelFileTree::index(int row, int column, const QModelIndex &parent) const
{
    const H5Node *p = node(parent);
    if (p == nullptr || row < 0 || row >= static_cast<int>(p->children.size()))
    {
        return {};
    }
    return createIndex(row, column, p->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ModelFileTree::parent(const QModelIndex &child) const
{
    const H5Node *n = node(child);
    if (!child.isValid() || n == nullptr || n->parent == nullptr || n->parent == root_.get())
    {
        return {};
    }
    const H5Node *p = n->parent;
    const H5Node *gp = p->parent;
    for (std::size_t i = 0; i < gp->children.size(); ++i)
    {
        if (gp->children[i].get() == p)
        {
            return createIndex(static_cast<int>(i), 0, const_cast<H5Node *>(p));
        }
    }
    return {};
}

int ModelFileTree::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }
    const H5Node *p = node(parent);
    return p == nullptr ? 0 : static_cast<int>(p->children.size());
}

int ModelFileTree::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant ModelFileTree::data(const QModelIndex &index, int role) const
{
    const H5Node *n = node(index);
    if (!index.isValid() || n == nullptr)
    {
        return {};
    }
    if (role == Qt::DisplayRole)
    {
        if (index.column() == 0)
        {
            return n->name;
        }
        return n->kind == H5Node::Kind::Dataset ? shape(*n) : QVariant();
    }
    if (role == Qt::DecorationRole && index.column() == 0)
    {
        return QApplication::style()->standardIcon(
            n->kind == H5Node::Kind::Group ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    }
    if (role == Qt::ToolTipRole)
    {
        QString tip = n->path;
        if (n->kind == H5Node::Kind::Dataset)
        {
            tip += QStringLiteral("\n%1 %2").arg(n->type, shape(*n));
        }
        if (!n->attributes.isEmpty())
        {
            tip += QStringLiteral("\n") + n->attributes.join(QStringLiteral("\n"));
        }
        return tip;
    }
    return {};
}

QVariant ModelFileTree::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        return section == 0 ? tr("Name") : tr("Shape");
    }
    return {};
}

} // namespace studio
