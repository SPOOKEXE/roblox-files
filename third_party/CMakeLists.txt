# Vendored dependencies. Warnings are suppressed; we do not maintain this code.
add_library(rbxl_lz4 STATIC lz4/lz4.c)
target_include_directories(rbxl_lz4 PUBLIC lz4)

file(GLOB ZSTD_SOURCES zstd/common/*.c zstd/compress/*.c zstd/decompress/*.c)
add_library(rbxl_zstd STATIC ${ZSTD_SOURCES})
target_include_directories(rbxl_zstd PUBLIC zstd)
target_include_directories(rbxl_zstd PRIVATE zstd/common)
# ZSTD_DISABLE_ASM keeps the build toolchain-independent (no .S files vendored).
target_compile_definitions(rbxl_zstd PRIVATE ZSTD_DISABLE_ASM=1 XXH_NAMESPACE=RBXL_ZSTD_)

add_library(rbxl_pugixml STATIC pugixml/pugixml.cpp)
target_include_directories(rbxl_pugixml PUBLIC pugixml)
# Only the DOM is used, never XPath; PUGIXML_NO_EXCEPTIONS removes pugixml's
# internal throws (xpath_exception, std::bad_alloc on OOM) so the library
# compiles under -fno-exceptions. PUBLIC because the macro also changes
# declarations in pugixml.hpp, so every consumer must see the same define.
target_compile_definitions(rbxl_pugixml PUBLIC PUGIXML_NO_EXCEPTIONS)

add_library(rbxl_doctest INTERFACE)
target_include_directories(rbxl_doctest INTERFACE doctest)

foreach(dep rbxl_lz4 rbxl_zstd rbxl_pugixml)
  set_target_properties(${dep} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(${dep} PRIVATE -w)
  endif()
endforeach()
