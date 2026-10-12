# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Franz X. Mohr

# The engine: everything that talks to BayesTS and to model files, and nothing
# that draws. A library of its own so the tests in test/ link exactly what the
# application runs.
add_library(studio_engine STATIC
    engine/BatchRunner.cpp
    engine/BatchRunner.h
    engine/CoefficientSummary.cpp
    engine/CoefficientSummary.h
    engine/Criteria.cpp
    engine/Criteria.h
    engine/DataSet.cpp
    engine/DataSet.h
    engine/HistoricalDecomposition.cpp
    engine/HistoricalDecomposition.h
    engine/Examples.cpp
    engine/Examples.h
    engine/HorseRace.cpp
    engine/HorseRace.h
    engine/ModelFileTree.cpp
    engine/Multipliers.cpp
    engine/Multipliers.h
    engine/ModelFileTree.h
    engine/ModelRunner.cpp
    engine/ModelRunner.h
    engine/QtReporter.cpp
    engine/PosteriorDraws.h
    engine/QtReporter.h
    engine/SignZero.cpp
    engine/SignZero.h
    engine/Spillover.cpp
    engine/Spillover.h
    engine/Extensions.cpp
    engine/Extensions.h
    engine/VolatilityBreak.cpp
    engine/VolatilityBreak.h
    engine/Structural.cpp
    engine/Structural.h
    engine/TableFormat.cpp
    engine/TableFormat.h
    engine/VarBuilder.cpp
    engine/VarBuilder.h
    engine/Workflow.cpp
    engine/Workflow.h
)
target_include_directories(studio_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(studio_engine PUBLIC
    Qt6::Widgets
    # BayesTS: the model front-ends, the model-file layer and the seeding the
    # command line uses, so a run here is the run `bayests` would make.
    BayesTS::models
    BayesTS::hdf5_helper
    BayesTS::subcommands
    BayesTS::core
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(studio_engine PUBLIC OpenMP::OpenMP_CXX)
endif()
# The data of the worked examples the studio ships (engine/Examples.cpp),
# read as ":/examples/<file>"; in the engine, so its tests read them too.
qt_add_resources(studio_engine "exampledata"
    PREFIX "/examples"
    BASE "engine/data"
    FILES engine/data/luetkepohl-e1.csv engine/data/luetkepohl-e6.csv
)

qt_add_executable(bayests-studio WIN32 MACOSX_BUNDLE
    $<$<PLATFORM_ID:Windows>:app/bayests-studio.rc>
    app/AssistantSmoke.cpp
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Theme.cpp
    app/Theme.h
    views/AssistantDialogs.cpp
    views/AssistantDialogs.h
    views/AssistantPanel.cpp
    views/AssistantPanel.h
    views/BuilderDialog.cpp
    views/BuilderDialog.h
    views/DiagnosticsView.cpp
    views/DiagnosticsView.h
    views/FigureExport.cpp
    views/FigureExport.h
    views/HistoricalDecompositionView.cpp
    views/HistoricalDecompositionView.h
    views/HorseRaceView.cpp
    views/HorseRaceView.h
    views/InclusionView.cpp
    views/InclusionView.h
    views/LiveBandsView.cpp
    views/LiveBandsView.h
    views/MultipliersView.cpp
    views/MultipliersView.h
    views/OverviewView.cpp
    views/OverviewView.h
    views/Palettes.cpp
    views/Palettes.h
    views/PaletteData.inc
    views/ExtensionsDialog.cpp
    views/ExtensionsDialog.h
    views/FontDialog.cpp
    views/FontDialog.h
    views/Fonts.cpp
    views/Fonts.h
    views/PaletteDialog.cpp
    views/VolatilityBreakView.cpp
    views/VolatilityBreakView.h
    views/PaletteDialog.h
    views/PlotStyle.cpp
    views/PlotStyle.h
    views/SeriesPlot.cpp
    views/SeriesPlot.h
    views/SignDialog.cpp
    views/SignDialog.h
    views/StagePages.cpp
    views/TableExport.cpp
    views/TableExport.h
    views/SpilloverView.cpp
    views/SpilloverView.h
    views/SummaryPlotView.cpp
    views/SummaryPlotView.h
    views/StructuralView.cpp
    views/StructuralView.h
    views/StagePages.h
)

target_compile_definitions(bayests-studio PRIVATE STUDIO_VERSION="${PROJECT_VERSION}")

# The window icon, in every size tools/make_icon.py draws; the .rc above
# gives the executable the same one.
qt_add_resources(bayests-studio "icons"
    PREFIX "/icons"
    BASE app/icons
    FILES
        app/icons/bayests-studio-16.png
        app/icons/bayests-studio-24.png
        app/icons/bayests-studio-32.png
        app/icons/bayests-studio-48.png
        app/icons/bayests-studio-64.png
        app/icons/bayests-studio-128.png
        app/icons/bayests-studio-256.png
)

# The font the studio ships (views/Fonts.h): Inter, sans serif.
qt_add_resources(bayests-studio "fonts"
    PREFIX "/fonts"
    BASE "app/fonts"
    FILES
        app/fonts/Inter-Regular.ttf
        app/fonts/Inter-Italic.ttf
        app/fonts/Inter-SemiBold.ttf
        app/fonts/Inter-Bold.ttf
        app/fonts/Inter-BoldItalic.ttf
)

# The studio's licence, for Help > Licence.
qt_add_resources(bayests-studio "licence"
    PREFIX "/licence"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/LICENSE"
)

target_link_libraries(bayests-studio PRIVATE
    studio_engine
    Qt6::Svg
    Qt6::Concurrent
    Qwt::Qwt
    BayesTS::openblas
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(studio_engine PRIVATE -Wall -Wextra)
    target_compile_options(bayests-studio PRIVATE -Wall -Wextra)
endif()

# The batch runner of a horse race runs the bayests command line, one process
# per model; it is looked for beside the application.
if(TARGET bayests)
    add_dependencies(bayests-studio bayests)
    add_custom_command(TARGET bayests-studio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:bayests> $<TARGET_FILE_DIR:bayests-studio>
        COMMENT "Copying bayests beside the studio")
endif()
