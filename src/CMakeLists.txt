# The engine: everything that talks to BayesTS and to model files, and nothing
# that draws. A library of its own so the tests in test/ link exactly what the
# application runs.
add_library(studio_engine STATIC
    engine/CoefficientSummary.cpp
    engine/CoefficientSummary.h
    engine/Criteria.cpp
    engine/Criteria.h
    engine/DataSet.cpp
    engine/DataSet.h
    engine/HistoricalDecomposition.cpp
    engine/HistoricalDecomposition.h
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
    engine/Structural.cpp
    engine/Structural.h
    engine/VarBuilder.cpp
    engine/VarBuilder.h
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

qt_add_executable(bayests-studio WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Theme.cpp
    app/Theme.h
    views/BuilderDialog.cpp
    views/BuilderDialog.h
    views/DiagnosticsView.cpp
    views/DiagnosticsView.h
    views/HistoricalDecompositionView.cpp
    views/HistoricalDecompositionView.h
    views/LiveBandsView.cpp
    views/LiveBandsView.h
    views/MultipliersView.cpp
    views/MultipliersView.h
    views/OverviewView.cpp
    views/OverviewView.h
    views/PlotStyle.cpp
    views/PlotStyle.h
    views/SeriesPlot.cpp
    views/SeriesPlot.h
    views/SignDialog.cpp
    views/SignDialog.h
    views/StagePages.cpp
    views/SpilloverView.cpp
    views/SpilloverView.h
    views/SummaryPlotView.cpp
    views/SummaryPlotView.h
    views/StructuralView.cpp
    views/StructuralView.h
    views/StagePages.h
)

target_compile_definitions(bayests-studio PRIVATE STUDIO_VERSION="${PROJECT_VERSION}")

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
