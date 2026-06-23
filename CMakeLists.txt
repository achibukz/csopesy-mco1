cmake_minimum_required(VERSION 3.14)
project(CSOPESY-MCO1 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

add_library(mco1_core
    src/config/Config.cpp
    src/scheduler/SchedulingPolicy.cpp
    src/scheduler/SchedulerEngine.cpp
    src/scheduler/CPU.cpp
    src/scheduler/Scheduler.cpp
    src/process/Process.cpp
    src/process/Instructions.cpp
    src/cli/Demo.cpp
)
target_include_directories(mco1_core PUBLIC include)
target_link_libraries(mco1_core PUBLIC Threads::Threads)

add_executable(mco1 src/main.cpp src/cli/Console.cpp)
target_link_libraries(mco1 PRIVATE mco1_core)

add_executable(scheduler_demo demos/scheduler_demo.cpp)
target_link_libraries(scheduler_demo PRIVATE mco1_core)

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(mco1_tests
        tests/MockProcess.cpp
        tests/test_config.cpp
        tests/test_policy.cpp
        tests/test_engine.cpp
        tests/test_scheduler.cpp
        tests/test_process.cpp
        tests/test_console.cpp
        src/cli/Console.cpp
    )
    target_link_libraries(mco1_tests PRIVATE mco1_core GTest::gtest_main)
    target_compile_definitions(mco1_tests PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    add_test(NAME mco1_tests COMMAND mco1_tests)
endif()
