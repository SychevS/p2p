cmake_minimum_required(VERSION 3.13)

project(p2p_network CXX)

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

macro(local_match_and_replace type from to)
    if ("${${type}}" MATCHES ${from})
        string(REGEX REPLACE ${from} ${to} ${type} ${${type}})
    endif()
endmacro()

macro(configure_msvc_flags)
  if (MSVC)
    if ("${CMAKE_CXX_FLAGS}" MATCHES "([/-]W[0-3])")
      message("Other Flags /W detected, replace it to avoid compiler warning to /W4")
      string(REGEX REPLACE "([/-]W[0-3])" "/W4" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    else()
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()

    add_compile_options(/wd4573)
  endif()
endmacro()

macro(configure_third_party_compiler_flags)
  if (MSVC)
    if ("${CMAKE_CXX_FLAGS}" MATCHES "([/-]W[1-4])")
      message("Other Flags /W detected, replace it to suppress third party warnings")
      local_match_and_replace(CMAKE_CXX_FLAGS "([/-]W[0-4])" "/W0")
      local_match_and_replace(CMAKE_C_FLAGS "([/-]W[0-4])" "/W0")
    else()
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W0")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W0")
    endif()
  endif()
endmacro()

configure_msvc_flags()

add_subdirectory(third-party)

set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS ON)

if(NOT APPLE)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(Boost 1.66 REQUIRED COMPONENTS system)
find_package(Threads)

add_library(${PROJECT_NAME} STATIC
  src/banman.h
  src/banman.cc
  src/connection.h
  src/connection.cc
  src/common.h
  src/common.cc
  src/host.h
  src/host.cc
  src/k_bucket.h
  src/kademlia_datagram.h
  src/kademlia_datagram.cc
  src/net_explorer.cc
  src/network.h
  src/network.cc
  src/pinger.cc
  src/routing_table.h
  src/routing_table.cc
  src/types.h
  src/udp.h
  src/third-party/sha1.h
  src/third-party/sha1.cc
  src/third-party/UPnP.h
  src/third-party/UPnP.cc
  src/utils/log.h
  src/utils/log.cc
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

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER src/host.h)

install(TARGETS ${PROJECT_NAME} bitcoin_utils easylogging libminiupnpc-static
        LIBRARY
          DESTINATION lib
        PUBLIC_HEADER
          DESTINATION include)
