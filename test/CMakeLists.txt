add_executable(studio_builder_test builder_test.cpp)
target_link_libraries(studio_builder_test PRIVATE studio_engine BayesTS::openblas)

# Single-threaded, as BayesTS's own fixture tests: draws are reproducible
# from the seed only then.
add_test(NAME builder.all_combinations COMMAND studio_builder_test)
set_tests_properties(builder.all_combinations PROPERTIES
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1")

# The studio's impulse responses and variance decompositions against
# bvartools', draw by draw. The reference files are generated, not checked in
# (one is 10 MB): `Rscript test/structural_reference.R test/reference`.
add_executable(studio_structural_test structural_test.cpp)
target_link_libraries(studio_structural_test PRIVATE studio_engine BayesTS::openblas)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reference/var2.h5")
    add_test(NAME structural.bvartools_reference
             COMMAND studio_structural_test "${CMAKE_CURRENT_SOURCE_DIR}/reference")
else()
    message(STATUS "test/reference missing: structural.bvartools_reference not registered "
                   "(Rscript test/structural_reference.R test/reference)")
endif()

# The table exports: CSV read back, LaTeX escaped and compiled with pdflatex
# where one is on PATH.
add_executable(studio_table_test table_test.cpp)
target_link_libraries(studio_table_test PRIVATE studio_engine)
add_test(NAME table.exports COMMAND studio_table_test)
