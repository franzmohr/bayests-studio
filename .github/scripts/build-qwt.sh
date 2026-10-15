#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# Builds Qwt against the Qt 6 qmake on PATH and installs it into a prefix,
# for the Linux builds: no distribution packages Qwt for Qt 6. FindQwt finds
# it with -DQwt_ROOT=<prefix>.
#
#   .github/scripts/build-qwt.sh <prefix> [qmake]
#
# The version and its checksum are pinned here; the designer plugin and the
# examples are switched off, which drops the build's need for Qt Designer.

set -euo pipefail

prefix=${1:?usage: build-qwt.sh <prefix> [qmake]}
qmake=${2:-qmake6}
version=6.3.0
sha256=dcb085896c28aaec5518cbc08c0ee2b4e60ada7ac929d82639f6189851a6129a

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL -o "$work/qwt.tar.bz2" \
    "https://downloads.sourceforge.net/project/qwt/qwt/$version/qwt-$version.tar.bz2"
echo "$sha256  $work/qwt.tar.bz2" | sha256sum -c -
tar -xjf "$work/qwt.tar.bz2" -C "$work"
cd "$work/qwt-$version"

# qwtconfig.pri names the install prefix and the parts to build.
sed -i \
    -e "s|^\(\s*QWT_INSTALL_PREFIX\s*=\).*|\1 $prefix|" \
    -e 's|^\(\s*QWT_CONFIG\s*+=\s*QwtDesigner\)|#\1|' \
    -e 's|^\(\s*QWT_CONFIG\s*+=\s*QwtExamples\)|#\1|' \
    -e 's|^\(\s*QWT_CONFIG\s*+=\s*QwtPlayground\)|#\1|' \
    -e 's|^\(\s*QWT_CONFIG\s*+=\s*QwtTests\)|#\1|' \
    qwtconfig.pri

"$qmake" qwt.pro
make -j"$(nproc)"
make install
ls "$prefix/lib" "$prefix/include/qwt_plot.h"
