project(NitroChromaflow)
cmake_minimum_required(VERSION 3.9.0)

set (PACKAGE_NAME NitroChromaflow)
set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 20)

# ── libpng ───────────────────────────────────────────────────────────────────
set(PNG_TESTS      OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS      OFF CACHE BOOL "" FORCE)
set(PNG_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
add_subdirectory(../cpp/libpng libpng)

# ── libtiff ──────────────────────────────────────────────────────────────────
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/libtiff/build)
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/../cpp/libtiff/build/CMakeLists.txt "# placeholder\n")
set(tiff-tools   OFF CACHE BOOL "" FORCE)
set(tiff-tests   OFF CACHE BOOL "" FORCE)
set(tiff-docs    OFF CACHE BOOL "" FORCE)
set(tiff-contrib OFF CACHE BOOL "" FORCE)
set(tiff-install OFF CACHE BOOL "" FORCE)
set(JPEG_SUPPORT  OFF CACHE BOOL "" FORCE)
set(LZMA_SUPPORT  OFF CACHE BOOL "" FORCE)
set(JBIG_SUPPORT  OFF CACHE BOOL "" FORCE)
set(WEBP_SUPPORT  OFF CACHE BOOL "" FORCE)
add_subdirectory(../cpp/libtiff libtiff)

# ── libjabcode ───────────────────────────────────────────────────────────────
file(GLOB JABCODE_SOURCES CONFIGURE_DEPENDS
    ../cpp/jabcode/src/jabcode/*.c
)
add_library(jabcode STATIC ${JABCODE_SOURCES})
target_include_directories(jabcode
    PUBLIC  ../cpp/jabcode/src/jabcode/include
    PRIVATE ../cpp/jabcode/src/jabcode
)
target_compile_features(jabcode PRIVATE c_std_11)
target_compile_options(jabcode  PRIVATE -O2)

# Define C++ library and add all sources
add_library(${PACKAGE_NAME} SHARED
        src/main/cpp/cpp-adapter.cpp
        ../cpp/HybridChromaFlow.cpp
        ../cpp/chromaflow_wasm.c
)

# Add Nitrogen specs :)
include(${CMAKE_SOURCE_DIR}/../nitrogen/generated/android/NitroChromaflow+autolinking.cmake)

# Set up local includes
include_directories(
        "src/main/cpp"
        "../cpp"
)

find_library(LOG_LIB log)

# Link all libraries together
target_link_libraries(
        ${PACKAGE_NAME}
        ${LOG_LIB}
        android                                   # <-- Android core
        jabcode
        tiff
        png_static
        z
)
