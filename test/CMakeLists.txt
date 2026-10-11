add_executable(studio_builder_test builder_test.cpp)
target_link_libraries(studio_builder_test PRIVATE studio_engine BayesTS::openblas)

# Single-threaded, as BayesTS's own fixture tests: draws are reproducible
# from the seed only then.
add_test(NAME builder.all_combinations COMMAND studio_builder_test)
set_tests_properties(builder.all_combinations PROPERTIES
    ENVIRONMENT "OMP_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1")
