# Headless encounter scenarios. Loads a pack and a scenario@1 file, runs the built-in
# nearest-enemy policy on both sides, and prints a deterministic outcome + digest.
add_executable(encounter_runner encounter_runner.cpp)
target_link_libraries(encounter_runner PRIVATE eng::sim)
target_compile_features(encounter_runner PRIVATE cxx_std_20)
set_target_properties(encounter_runner PROPERTIES CXX_EXTENSIONS OFF)
_engine_warnings(encounter_runner)

if(ENGINE_BUILD_TESTS)
    file(GLOB _scenarios CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/scenarios/*/scenario.json")
    foreach(_scenario IN LISTS _scenarios)
        get_filename_component(_dir "${_scenario}" DIRECTORY)
        get_filename_component(_name "${_dir}" NAME)
        add_test(NAME "encounter_scenario_${_name}"
                 COMMAND ${CMAKE_COMMAND}
                         -DRUNNER=$<TARGET_FILE:encounter_runner>
                         -DSCENARIO=${_scenario}
                         -DGOLDEN=${_dir}/golden.txt
                         -DROOT=${CMAKE_SOURCE_DIR}
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/check_scenario.cmake)
        set_tests_properties("encounter_scenario_${_name}" PROPERTIES TIMEOUT 120)
    endforeach()
endif()
