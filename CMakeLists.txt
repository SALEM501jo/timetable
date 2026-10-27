cmake_minimum_required(VERSION 3.20)
project(timetable VERSION 0.1.0 DESCRIPTION "Conflict-free course timetable scheduler" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TIMETABLE_BUILD_TESTS "Build the unit tests" ON)
option(TIMETABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(timetable_setup target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(TIMETABLE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(TIMETABLE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  # Self-contained .exe files on MinGW, so they run without the compiler on PATH.
  if(MINGW)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
      target_link_options(${target} PRIVATE -static)
    endif()
  endif()
endfunction()

add_library(timetable
  src/models.cpp
  src/csv.cpp
  src/validator.cpp
  src/scheduler.cpp
  src/report.cpp
)
add_library(timetable::timetable ALIAS timetable)
target_include_directories(timetable PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(timetable PUBLIC cxx_std_17)
timetable_setup(timetable)

add_executable(timetable_cli cli/main.cpp)
target_link_libraries(timetable_cli PRIVATE timetable)
timetable_setup(timetable_cli)

if(TIMETABLE_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(timetable_tests
    tests/test_csv.cpp
    tests/test_validator.cpp
    tests/test_scheduler.cpp
    tests/test_report.cpp
  )
  target_link_libraries(timetable_tests PRIVATE timetable Catch2::Catch2WithMain)
  target_compile_definitions(timetable_tests PRIVATE TIMETABLE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  timetable_setup(timetable_tests)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(timetable_tests)
endif()
