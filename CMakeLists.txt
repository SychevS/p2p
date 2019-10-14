cmake_minimum_required(VERSION 3.10)

project(p2p_network)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
endif()

add_subdirectory(third-party)

find_package(Boost 1.66 REQUIRED COMPONENTS system)
find_package(Threads)

add_library(${PROJECT_NAME} STATIC
  src/common.h
  src/k_bucket.h
  src/serialization.h
  src/serialization.cc
  src/types.h
  src/udp.h)

target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} Boost::system bitcoin_utils
  ${CMAKE_THREAD_LIBS_INIT})

if(NOT MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unknown-pragmas -pedantic)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE /W4)
endif()
