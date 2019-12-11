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

set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS ON)

if(NOT APPLE)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(Boost 1.66 REQUIRED COMPONENTS system)
find_package(Threads)

add_library(${PROJECT_NAME} STATIC
  src/connection.h
  src/connection.cc
  src/common.h
  src/common.cc
  src/host.h
  src/host.cc
  src/k_bucket.h
  src/kademlia_datagram.h
  src/kademlia_datagram.cc
  src/routing_table.h
  src/routing_table.cc
  src/types.h
  src/udp.h
  src/third-party/UPnP.h
  src/third-party/UPnP.cpp
  src/utils/log.h
  src/utils/log.cc
  src/utils/murmurhash2.h
  src/utils/localip.h
  src/utils/serialization.h
  src/utils/serialization.cc)

if(MSVC)
  target_sources(${PROJECT_NAME} PRIVATE src/utils/localip_windows.cc)
else()
  target_sources(${PROJECT_NAME} PRIVATE src/utils/localip_posix.cc)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} Boost::system bitcoin_utils easylogging libminiupnpc-static
  ${CMAKE_THREAD_LIBS_INIT})

if(NOT MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unknown-pragmas -pedantic)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE /W4)
endif()
