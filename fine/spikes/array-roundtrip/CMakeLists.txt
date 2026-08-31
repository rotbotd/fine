cmake_minimum_required(VERSION 3.20)
project(fine_array_roundtrip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(Z3_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(Z3_BUILD "${Z3_SOURCE}/build/fine" CACHE PATH
    "Existing static Z3 build directory (configured with Z3_BUILD_LIBZ3_SHARED=OFF)")

if(NOT EXISTS "${Z3_BUILD}/libz3.a")
  message(FATAL_ERROR
    "${Z3_BUILD}/libz3.a does not exist; build the repository's libz3 target first")
endif()

add_executable(array-roundtrip array_roundtrip.cpp)
target_include_directories(array-roundtrip PRIVATE
  "${Z3_SOURCE}/src"
  "${Z3_BUILD}/src")
# These must match the ordinary Debug/static checkout configuration used by
# the build command in README.md. In particular, _MP_INTERNAL selects Z3's
# bundled bigint implementation rather than changing the header ABI to GMP.
target_compile_definitions(array-roundtrip PRIVATE
  _MP_INTERNAL
  Z3DEBUG
  _TRACE)
target_link_libraries(array-roundtrip PRIVATE "${Z3_BUILD}/libz3.a")
