cmake_minimum_required(VERSION 3.20)
project(transformers-bnlang LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ----------------------------------------------------------------------
# Platform triple — mirrors onnxruntime-bnlang convention.
# ----------------------------------------------------------------------
if(NOT BNL_TARGET_TRIPLE)
    if(WIN32)
        set(BNL_TARGET_TRIPLE "windows-x64")
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            set(BNL_TARGET_TRIPLE "darwin-arm64")
        else()
            set(BNL_TARGET_TRIPLE "darwin-x64")
        endif()
    else()
        set(BNL_TARGET_TRIPLE "linux-x64")
    endif()
endif()
message(STATUS "Target triple: ${BNL_TARGET_TRIPLE}")

# ----------------------------------------------------------------------
# Native plugin. The bnl wrapper depends on onnxruntime-bnlang at the
# *bnl* level (deps in bnl.json), so this native lib stays focused on
# tokenization + sampler hot paths.
# ----------------------------------------------------------------------
add_library(transformers-bnlang SHARED
    src/main.cpp
    src/bpe.cpp
)

target_include_directories(transformers-bnlang PRIVATE src)

# Land artifact under build/<triple>/ with no `lib` prefix on Unix,
# matching bnl.json's `targets` map.
set_target_properties(transformers-bnlang PROPERTIES
    PREFIX                            ""
    RUNTIME_OUTPUT_DIRECTORY          "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY          "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE  "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE  "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG    "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG    "${CMAKE_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(transformers-bnlang PRIVATE /W4 /utf-8)
else()
    target_compile_options(transformers-bnlang PRIVATE -Wall -Wextra)
endif()
