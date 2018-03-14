if(NOT CMAKE_PROJECT_NAME STREQUAL ROOT)
  cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
  project(RooFit)
  find_package(ROOT REQUIRED)
  include(${ROOT_USE_FILE})
endif()

if(mathmore OR ROOT_mathmore_FOUND)
  set(ROOT_MATHMORE_LIBRARY MathMore)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/roofit/inc) 
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/roofitcore/inc) 
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/roostats/inc) 

add_root_subdirectory(roofitcore ALL)
add_root_subdirectory(roofit ALL)
add_root_subdirectory(roostats ALL)
if(xml OR ROOT_xml_FOUND)
  add_root_subdirectory(histfactory ALL)
endif()


