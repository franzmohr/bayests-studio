qt_add_executable(bayests-studio WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Theme.cpp
    app/Theme.h
    engine/Criteria.cpp
    engine/Criteria.h
    engine/ModelFileTree.cpp
    engine/ModelFileTree.h
    engine/ModelRunner.cpp
    engine/ModelRunner.h
    engine/QtReporter.cpp
    engine/QtReporter.h
    views/DiagnosticsView.cpp
    views/DiagnosticsView.h
    views/LiveBandsView.cpp
    views/LiveBandsView.h
    views/OverviewView.cpp
    views/OverviewView.h
    views/PlotStyle.cpp
    views/PlotStyle.h
    views/SeriesPlot.cpp
    views/SeriesPlot.h
    views/StagePages.cpp
    views/StagePages.h
)

target_include_directories(bayests-studio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(bayests-studio PRIVATE STUDIO_VERSION="${PROJECT_VERSION}")

target_link_libraries(bayests-studio PRIVATE
    Qt6::Widgets
    Qt6::Svg
    Qt6::Concurrent
    Qwt::Qwt
    # BayesTS: the model front-ends, the model-file layer and the seeding the
    # command line uses, so a run here is the run `bayests` would make.
    BayesTS::models
    BayesTS::hdf5_helper
    BayesTS::subcommands
    BayesTS::core
    BayesTS::openblas
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(bayests-studio PRIVATE OpenMP::OpenMP_CXX)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bayests-studio PRIVATE -Wall -Wextra)
endif()
