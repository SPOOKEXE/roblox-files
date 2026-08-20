add_executable(rbxl_tests
  main.cpp
  test_result.cpp
  test_version.cpp
  test_deps.cpp
  test_bitutil.cpp
  test_variant.cpp
  test_dom.cpp
  test_chunk.cpp)
target_link_libraries(rbxl_tests PRIVATE rbxl rbxl_doctest rbxl_lz4 rbxl_zstd rbxl_pugixml)
target_include_directories(rbxl_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(rbxl_tests PRIVATE RBXL_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/temp")
add_test(NAME rbxl_tests COMMAND rbxl_tests)
