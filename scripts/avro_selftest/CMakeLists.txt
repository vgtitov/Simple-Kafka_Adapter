cmake_minimum_required(VERSION 3.20)
project(avro_selftest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same workaround as the main CMakeLists: some avro-cpp ports ship a config that
# aliases avro-cpp::avrocpp before defining it. Pre-create it as a stub.
if(NOT TARGET avro-cpp::avrocpp)
    add_library(avro-cpp::avrocpp INTERFACE IMPORTED)
endif()
find_package(avro-cpp CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_executable(avro_selftest main.cpp)

# Mirror the link selection used by the main component (src target).
if(TARGET avro-cpp::avrocpp_s)
    target_link_libraries(avro_selftest PRIVATE avro-cpp::avrocpp_s)
elseif(TARGET avro-cpp::avrocpp_static)
    target_link_libraries(avro_selftest PRIVATE avro-cpp::avrocpp_static)
elseif(TARGET avro-cpp::avrocpp)
    target_link_libraries(avro_selftest PRIVATE avro-cpp::avrocpp)
else()
    message(FATAL_ERROR "Could not find avro-cpp target")
endif()

target_link_libraries(avro_selftest PRIVATE fmt::fmt)

# Same fmt >= 11 workaround as the main component: avro/Exception.hh needs
# fmt::format() which now lives in <fmt/format.h>.
if (MSVC)
    target_compile_options(avro_selftest PRIVATE /FI fmt/format.h)
else()
    target_compile_options(avro_selftest PRIVATE -include fmt/format.h)
endif()
