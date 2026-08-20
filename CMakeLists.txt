cmake_minimum_required(VERSION 3.16)
project(roblox_rbxl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RBXL_BUILD_CLI "Build the rbxl command line tool" OFF)   # Task 15 flips this to ON
option(RBXL_BUILD_TESTS "Build the test suite" ON)

add_subdirectory(third_party)

add_library(rbxl STATIC)
add_library(rbxl::rbxl ALIAS rbxl)
target_include_directories(rbxl PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(rbxl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbxl PRIVATE rbxl_lz4 rbxl_zstd rbxl_pugixml)
if(NOT MSVC)
  target_compile_options(rbxl PRIVATE -Wall -Wextra -Wpedantic)
else()
  target_compile_options(rbxl PRIVATE /W4)
endif()
# Sources are appended by later tasks via target_sources in src/CMakeLists.txt.
add_subdirectory(src)

if(RBXL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(RBXL_BUILD_CLI)
  add_subdirectory(tools/rbxl-cli)
endif()
