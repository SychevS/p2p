cmake_minimum_required(VERSION 3.10)

project(bitcoin_utils)

add_library(${PROJECT_NAME} STATIC
  arith_uint256.h
  arith_uint256.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
