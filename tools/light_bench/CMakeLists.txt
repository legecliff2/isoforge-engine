# R-G13 phase 1 light rebuild benchmark (LIGHTING.md §12). Report-only in ctest (CI machines vary and Debug is far
# slower); run `light_bench --enforce` on a Release build to check the 2 ms budget.
add_executable(light_bench light_bench.cpp)
target_link_libraries(light_bench PRIVATE eng::sim)
target_compile_features(light_bench PRIVATE cxx_std_20)
add_test(NAME light_bench_smoke COMMAND light_bench --quick)
