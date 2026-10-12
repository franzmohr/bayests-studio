# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# Finds Qwt built against Qt 6 and defines the imported target Qwt::Qwt.
#
# MSYS2 (mingw-w64-*-qwt-qt6), Homebrew (qwt) and most Linux distributions
# (libqwt-qt6-dev) install a pkg-config file; a hand-built Qwt is found by
# setting Qwt_ROOT to its install prefix.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_Qwt QUIET Qt6Qwt6 qwt-qt6 Qwt6)
endif()

find_path(Qwt_INCLUDE_DIR qwt_plot.h
    HINTS ${PC_Qwt_INCLUDE_DIRS}
    PATH_SUFFIXES qwt-qt6 qwt6 qwt)
find_library(Qwt_LIBRARY
    NAMES qwt-qt6 qwt6-qt6 qwt
    HINTS ${PC_Qwt_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Qwt REQUIRED_VARS Qwt_LIBRARY Qwt_INCLUDE_DIR)

if(Qwt_FOUND AND NOT TARGET Qwt::Qwt)
    find_package(Qt6 REQUIRED COMPONENTS PrintSupport OpenGL)
    add_library(Qwt::Qwt UNKNOWN IMPORTED)
    set_target_properties(Qwt::Qwt PROPERTIES
        IMPORTED_LOCATION "${Qwt_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Qwt_INCLUDE_DIR}"
        INTERFACE_COMPILE_DEFINITIONS "QWT_DLL"
        INTERFACE_LINK_LIBRARIES "Qt6::Widgets;Qt6::PrintSupport;Qt6::Svg;Qt6::OpenGL;Qt6::Concurrent")
endif()
