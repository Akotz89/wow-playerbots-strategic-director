cmake_minimum_required(VERSION 3.20)
# Keep even this standalone package behind the repository's regular-PC WSL2
# guard.  This runs before project() enables a compiler or performs probes.
get_filename_component(PBGUIDE_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
execute_process(COMMAND "${PBGUIDE_REPOSITORY_ROOT}/scripts/wow-build-host-guard"
    RESULT_VARIABLE PBGUIDE_GUARD_RESULT OUTPUT_VARIABLE PBGUIDE_GUARD_OUTPUT ERROR_VARIABLE PBGUIDE_GUARD_ERROR)
if(NOT PBGUIDE_GUARD_RESULT STREQUAL "0")
    message(FATAL_ERROR "WoW build-host guard refused: ${PBGUIDE_GUARD_ERROR}")
endif()
project(playerbots_strategic_director LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost 1.81 REQUIRED COMPONENTS json)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PBGUIDE_BUILD_PYTHON_GEAR "Build the in-process CPython gear companion" OFF)

add_library(playerbots_strategic_director
    src/activity_reservations.cpp
    src/director.cpp
    src/failure_memory.cpp
    src/canonical_item_sources.cpp
    src/gear_planner.cpp
    src/route_graph.cpp
)

target_link_libraries(playerbots_strategic_director PUBLIC Boost::json OpenSSL::Crypto)

target_include_directories(playerbots_strategic_director
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(playerbots_strategic_director
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
)

add_library(playerbots_strategic_replay
    src/gear_plan_json.cpp
    src/gear_plan_store.cpp
    src/replay.cpp
)
target_include_directories(playerbots_strategic_replay
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(playerbots_strategic_replay
    PUBLIC
        playerbots_strategic_director
        Boost::json
)
target_compile_options(playerbots_strategic_replay
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
)

add_executable(playerbots_strategic_replay_cli
    tools/replay_main.cpp
)
target_link_libraries(playerbots_strategic_replay_cli
    PRIVATE playerbots_strategic_replay
)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(gear_plan_persistence_tests tests/gear_plan_persistence_tests.cpp)
    target_link_libraries(gear_plan_persistence_tests PRIVATE playerbots_strategic_replay)
    target_compile_definitions(gear_plan_persistence_tests PRIVATE CANONICAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/canonical-item-sources")
    target_link_options(gear_plan_persistence_tests PRIVATE -Wl,--wrap=renameat -Wl,--wrap=fsync -Wl,--wrap=write -Wl,--wrap=read -Wl,--wrap=close -Wl,--wrap=openat)
    add_test(NAME gear_plan_persistence_tests COMMAND gear_plan_persistence_tests)
    add_test(NAME gear_plan_store_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gear_plan_store_integration.py $<TARGET_FILE:gear_plan_persistence_tests> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/canonical-item-sources/cases.json)
    set_tests_properties(gear_plan_store_integration PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(v2_acquisition_metric_tests tests/v2_acquisition_metric_tests.cpp)
    target_link_libraries(v2_acquisition_metric_tests PRIVATE playerbots_strategic_replay)
    target_compile_definitions(v2_acquisition_metric_tests PRIVATE CANONICAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/canonical-item-sources")
    add_test(NAME v2_acquisition_metric_tests COMMAND v2_acquisition_metric_tests)
    add_executable(canonical_item_source_tests tests/canonical_item_source_tests.cpp)
    target_link_libraries(canonical_item_source_tests PRIVATE playerbots_strategic_replay)
    target_compile_definitions(canonical_item_source_tests PRIVATE CANONICAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/canonical-item-sources")
    add_test(NAME canonical_item_source_tests COMMAND canonical_item_source_tests)
    add_test(NAME canonical_schema_parity COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/canonical_schema_parity.py)
    set_tests_properties(canonical_schema_parity PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(playerbots_strategic_director_tests
        tests/director_tests.cpp
    )
    target_link_libraries(playerbots_strategic_director_tests
        PRIVATE playerbots_strategic_replay
    )
    target_compile_options(playerbots_strategic_director_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
    )
    add_test(
        NAME playerbots_strategic_director_tests
        COMMAND playerbots_strategic_director_tests
    )
    add_test(
        NAME playerbots_strategic_replay_fixture
        COMMAND playerbots_strategic_replay_cli
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/northshire-basic.replay.json
    )
    add_test(
        NAME playerbots_content_graph_compiler_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/content_graph_compiler_tests.py
    )
    add_test(
        NAME playerbots_route_catalog_compiler_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/route_catalog_compiler_tests.py
    )
    add_test(
        NAME playerbots_route_catalog_digest_fixture
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_route_catalog.py
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/northshire-contract.route.json
            --verify
    )
    set_tests_properties(
        playerbots_content_graph_compiler_tests
        playerbots_route_catalog_compiler_tests
        playerbots_route_catalog_digest_fixture
        PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
    )
    add_test(
        NAME playerbots_schema_validation
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_schemas.py
    )
endif()

if(PBGUIDE_BUILD_PYTHON_GEAR)
    find_package(Python3 REQUIRED COMPONENTS Development.Module)
    set_target_properties(playerbots_strategic_director playerbots_strategic_replay PROPERTIES POSITION_INDEPENDENT_CODE ON)
    Python3_add_library(_pbguide_gear MODULE WITH_SOABI
        ${CMAKE_CURRENT_SOURCE_DIR}/src/python_gear.cpp)
    target_include_directories(_pbguide_gear PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(_pbguide_gear PRIVATE playerbots_strategic_replay)
    if(BUILD_TESTING)
        add_test(NAME pbguide_python_v2_vendor_consumer
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/../../integration/cpi-225-native-earned-upgrade-20261021/tests/test_native_acquisition_v2_consumer.py)
        set_tests_properties(pbguide_python_v2_vendor_consumer PROPERTIES ENVIRONMENT
            "PYTHONPATH=$<TARGET_FILE_DIR:_pbguide_gear>;PBGUIDE_TEST_PACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR};PBGUIDE_TEST_CONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../integration/cpi-225-native-earned-upgrade-20261021/runtime/population/python/lib")
    endif()
endif()
