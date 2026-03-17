cmake_minimum_required(VERSION 3.16)
project(helios-balancer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(
  -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
  $<$<CONFIG:Debug>:-g3 -O0>
  $<$<CONFIG:Release>:-O2>
)

set(SOURCES
  src/main.cpp
  src/config.cpp
)

add_executable(helios ${SOURCES})
target_include_directories(helios PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(helios PRIVATE Threads::Threads)

install(TARGETS helios DESTINATION bin)
install(FILES config/helios.conf DESTINATION etc/helios)
