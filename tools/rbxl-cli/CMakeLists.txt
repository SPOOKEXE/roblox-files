add_executable(rbxl_cli main.cpp)
set_target_properties(rbxl_cli PROPERTIES OUTPUT_NAME rbxl)
target_link_libraries(rbxl_cli PRIVATE rbxl)
target_include_directories(rbxl_cli PRIVATE ${CMAKE_SOURCE_DIR}/src)
