// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

#include "engine/Examples.h"

namespace studio
{

/// Extensions are JSON files the studio reads at start-up and on request:
/// worked examples for the assistant, and colour palettes. A file is one
/// object,
///
///   { "studio-extension": 1, "examples": [ ... ], "palettes": [ ... ] }
///
/// either array optional. They are read from the user's extensions folder
/// (every *.json in it) and from every reproduction project that carries a
/// studio-example.json, so a paper added to the reproductions folder with one
/// is offered as an example. Paths in a file are relative to its folder.
/// README.md in the extensions folder, written with the template, describes
/// every field.
constexpr int kExtensionFormat = 1;

/// What reading one file gave.
struct ExtensionReport
{
    QString file;
    QStringList examples;   ///< the ids read
    QStringList palettes;   ///< the ids read
    QStringList warnings;   ///< unknown keys, notes for steps the workflow lacks, ...
    QString error;          ///< the file was not read at all
};

/// The user's extensions folder: <application data>/extensions, or the
/// folder STUDIO_EXTENSIONS names.
QString userExtensionsFolder();

/// Every extension file there is: the user's folder's *.json, then each
/// <reproductions>/<project>/studio-example.json.
QStringList extensionFiles(const QString &userFolder, const QString &reproductions);

/// extensionFiles() of the user's folder and of the reproductions folder the
/// assistant was last pointed at.
QStringList defaultExtensionFiles();

/// Reads the files, installs their examples (setExtensionExamples) and keeps
/// their palettes for views/Palettes (extensionPalettes), and says what each
/// file gave. A file that fails to read contributes nothing.
std::vector<ExtensionReport> loadExtensions(const QStringList &files);

/// What the last load said, file by file.
const std::vector<ExtensionReport> &lastExtensionReports();

/// The palette objects the last load read, with the file each came from;
/// views/Palettes turns them into palettes.
struct ExtensionPalette
{
    QString file;
    QJsonObject json;
};
const std::vector<ExtensionPalette> &extensionPalettes();

/// An example as a file stores it, and back. `base` is the folder the
/// file's paths are relative to. exampleFromJson throws, naming the field,
/// where a field is missing or wrong, and adds to `warnings` what it ignores.
QJsonObject exampleToJson(const WorkflowExample &example);
WorkflowExample exampleFromJson(const QJsonObject &json, const QString &base, QStringList *warnings = nullptr);

/// Writes README.md, describing the format, and a template -- one of the
/// studio's own examples and a palette, ready to copy -- into `folder`.
/// Returns the template's path.
QString writeExtensionTemplate(const QString &folder);

} // namespace studio
