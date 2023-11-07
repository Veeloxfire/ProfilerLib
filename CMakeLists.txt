cmake_minimum_required(VERSION 3.10)
project(Tracer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(Tracer STATIC "${PROJECT_SOURCE_DIR}/src/trace_win.cpp")
target_include_directories(Tracer PRIVATE "${PROJECT_SOURCE_DIR}/include/")
target_sources(Tracer PUBLIC 
  FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}
  FILES "${PROJECT_SOURCE_DIR}/include/Tracer/trace.h"
)