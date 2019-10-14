cmake_minimum_required(VERSION 3.10)

project(easylogging CXX)

add_library(${PROJECT_NAME} STATIC
  src/easylogging++.h
  src/easylogging++.cc)

target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
