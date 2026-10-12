# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

cmake_minimum_required(VERSION 3.25)

# BayesTS's top level declares Fortran (for the BLAS it finds), so this project
# has to enable it too: languages are enabled per top-level project.
project(BayesTSStudio
    VERSION 0.1.0
    LANGUAGES C CXX Fortran
    DESCRIPTION "Desktop application for specifying, estimating and analysing BayesTS models"
)

# The executable's icon is a Windows resource.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#####################################################################
# BayesTS
#
# Pulled in as a subproject and pinned to a commit, so a build of the studio
# always names the sampler code it ships. For working on both at once, point
# FetchContent at a local checkout instead of the pinned commit:
#
#   -DFETCHCONTENT_SOURCE_DIR_BAYESTS=D:/projects/BayesTS
#
# (the user presets do this). BayesTS is BSD-3-Clause; its licence text is
# installed with the application and shown under Help > Third-party licences.

include(FetchContent)

set(STUDIO_BAYESTS_GIT_TAG "main" CACHE STRING
    "BayesTS commit or tag to build against; pin a release tag for shipping builds")

FetchContent_Declare(BayesTS
    GIT_REPOSITORY https://github.com/franzmohr/BayesTS.git
    GIT_TAG        ${STUDIO_BAYESTS_GIT_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(BayesTS)

#####################################################################
# Qt and Qwt
#
# Licensing constraint (see LICENSING.md): the application is BSD-3-Clause, so
# only Qt modules available under the LGPL may be used -- Core, Gui, Widgets,
# Svg, Concurrent. Qt Charts, Qt Graphs and Qt Data Visualization are GPL-only
# in the open-source edition and would make the distributed studio GPL. Qt is
# linked dynamically, which is what the LGPL requires.
#
# Plots are drawn with Qwt (Qwt License 1.0: LGPL 2.1 with exceptions that
# permit any linking).

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent)
qt_standard_project_setup()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
find_package(Qwt REQUIRED)

add_subdirectory(src)

include(StudioInstall)
add_subdirectory(packaging/msix)
add_subdirectory(packaging/nsis)

enable_testing()
add_subdirectory(test)
