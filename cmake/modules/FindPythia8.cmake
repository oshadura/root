# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

# Find the Pythia8 includes and library.
#
# This module defines
# PYTHIA8_INCLUDE_DIR   where to locate Pythia.h file
# PYTHIA8_LIBRARY       where to find the libpythia8 library
# PYTHIA8_<lib>_LIBRARY Addicional libraries
# PYTHIA8_LIBRARIES     (not cached) the libraries to link against to use Pythia8
# PYTHIA8_FOUND         if false, you cannot build anything that requires Pythia8
# PYTHIA8_VERSION       version of Pythia8 if found

set(_pythia8dirs
    ${PYTHIA8}
    $ENV{PYTHIA8}
    ${PYTHIA8_DIR}
    $ENV{PYTHIA8_DIR}
    /usr
    /opt/pythia8)

find_path(PYTHIA8_INCLUDE_DIR
          NAMES Pythia8/Pythia.h
          HINTS ${_pythia8dirs}
          PATH_SUFFIXES include include/Pythia8 include/pythia8
          DOC "Specify the directory containing Pythia.h.")

find_library(PYTHIA8_LIBRARY
             NAMES pythia8 Pythia8
             HINTS ${_pythia8dirs}
             PATH_SUFFIXES lib
             DOC "Specify the Pythia8 library here.")

find_library(PYTHIA8_hepmcinterface_LIBRARY
             NAMES hepmcinterface pythia8tohepmc
             HINTS ${_pythia8dirs}
             PATH_SUFFIXES lib)

find_library(PYTHIA8_lhapdfdummy_LIBRARY
             NAMES lhapdfdummy
             HINTS ${_pythia8dirs}
             PATH_SUFFIXES lib)

foreach(_lib PYTHIA8_LIBRARY PYTHIA8_hepmcinterface_LIBRARY PYTHIA8_lhapdfdummy_LIBRARY)
  if(${_lib})
    set(PYTHIA8_LIBRARIES ${PYTHIA8_LIBRARIES} ${${_lib}})
  endif()
endforeach()
set(PYTHIA8_INCLUDE_DIRS ${PYTHIA8_INCLUDE_DIR} ${PYTHIA8_INCLUDE_DIR}/Pythia8 )

find_path(PYTHIA8_DATA 
          NAMES MainProgramSettings.xml
          HINTS ${_pythia8dirs} ${_pythia8dirs}/share/Pythia8
          PATH_SUFFIXES xmldoc
          DOC "Specify the Pythia8 data directory here.")

if(PYTHIA8_INCLUDE_DIR AND PYTHIA8_LIBRARY)
  set(PYTHIA8_FOUND TRUE)
endif()      
       
if(PYTHIA8_FOUND)
  message(STATUS "Found Pythia8 library: ${PYTHIA8_LIBRARY}")
  message(STATUS "Found Pythia8 include directory: ${PYTHIA8_INCLUDE_DIR}")
endif()

if(PYTHIA8_DATA)
  message(STATUS "Found Pythia8 data directory to be used in tutorials: ${PYTHIA8_DATA}")
else()
  message(STATUS "Pythia8 data directory used in tutorials will be found through environment variable PYTHIA8DATA")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pythia8 FOUND_VAR PYTHIA8_FOUND
                                          REQUIRED_VARS PYTHIA8_INCLUDE_DIR PYTHIA8_LIBRARY 
                                          DEFAULT_MSG)
mark_as_advanced(PYTHIA8_FOUND PYTHIA8_INCLUDE_DIR PYTHIA8_LIBRARY PYTHIA8_hepmcinterface_LIBRARY PYTHIA8_lhapdfdummy_LIBRARY)
