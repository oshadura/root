# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

set(root_build_options)

#---------------------------------------------------------------------------------------------------
#---ROOT_BUILD_FEATURE( name defvalue [description] )
#---------------------------------------------------------------------------------------------------
function(ROOT_BUILD_FEATURE opt defvalue)
  if(ARGN)
    set(description ${ARGN})
  else()
    set(description " ")
  endif()
  set(${opt}_defvalue    ${defvalue} PARENT_SCOPE)
  set(${opt}_description ${description} PARENT_SCOPE)
  set(root_build_options  ${root_build_options} ${opt} PARENT_SCOPE )
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_APPLY_FEATURES()
#---------------------------------------------------------------------------------------------------
function(ROOT_APPLY_FEATURES)
  foreach(opt ${root_build_options})
     option(${opt} "${${opt}_description}" ${${opt}_defvalue})
  endforeach()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_GET_FEATURES(result ENABLED)
#---------------------------------------------------------------------------------------------------
function(ROOT_GET_FEATURES result)
  CMAKE_PARSE_ARGUMENTS(ARG "ENABLED" "" "" ${ARGN})
  set(enabled)
  foreach(opt ${root_build_options})
    if(ARG_ENABLED)
      if(${opt})
        set(enabled "${enabled} ${opt}")
      endif()
    else()
      set(enabled "${enabled} ${opt}")
    endif()
  endforeach()
  set(${result} "${enabled}" PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_ENABLED_FEATURES()
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_ENABLED_FEATURES)
  set(enabled_opts)
  ROOT_GET_FEATURES(enabled_opts ENABLED)
  foreach(opt ${enabled_opts})
    message(STATUS "Enabled support for: ${opt}")
  endforeach()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_WRITE_FEATURES(file )
#---------------------------------------------------------------------------------------------------
function(ROOT_WRITE_FEATURES file)
  file(WRITE ${file} "#---Options enabled for the build of ROOT-----------------------------------------------\n")
  foreach(opt ${root_build_options})
    if(${opt})
      file(APPEND ${file} "set(${opt} ON)\n")
    else()
      file(APPEND ${file} "set(${opt} OFF)\n")
    endif()
  endforeach()
endfunction()

set(compression_default "lz4" CACHE STRING "ROOT compression algorithm used as a default, default option is lz4. Can be lz4, zlib, or lzma")
set(gcctoolchain "" CACHE PATH "Path for the gcctoolchain in case not the system gcc is used to build clang/LLVM")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
   ROOT_BUILD_FEATURE(ccache ON "Enable ccache usage for speeding up builds")
else()
   ROOT_BUILD_FEATURE(ccache OFF "Enable ccache usage for speeding up builds")
endif()
# C++ standarts features
ROOT_BUILD_FEATURE(cxx11 ON "Build using C++11 compatible mode, requires gcc > 4.7.x or clang")
ROOT_BUILD_FEATURE(cxx14 OFF "Build using C++14 compatible mode, requires gcc > 4.9.x or clang")
ROOT_BUILD_FEATURE(cxx17 OFF "Build using C++17 compatible mode, requires gcc >= 7.2.0 or clang")
# Interpreter features
ROOT_BUILD_FEATURE(cling ON "Enable CLING C++ interpreter")
# C++ modules features
ROOT_BUILD_FEATURE(cxxmodules OFF "Compile with C++ modules enabled.")
ROOT_BUILD_FEATURE(runtime_cxxmodules OFF "Enable runtime support for C++ modules.")
# ROOT pch features
ROOT_BUILD_FEATURE(pch ON "Generation of PCH for ROOT")
# ROOT build features
ROOT_BUILD_FEATURE(exceptions ON "Turn on compiler exception handling capability")
ROOT_BUILD_FEATURE(explicitlink ON "Explicitly link with all dependent libraries")
ROOT_BUILD_FEATURE(rpath OFF "Set run-time library load path on executables and shared libraries (at installation area)")
ROOT_BUILD_FEATURE(shared ON "Use shared 3rd party libraries if possible")
# ROOT concurency features
ROOT_BUILD_FEATURE(imt ON "Implicit multi-threading support")
ROOT_BUILD_FEATURE(thread ON "Using thread library (cannot be disabled)")
# Coverage features
ROOT_BUILD_FEATURE(coverage OFF "Test coverage")
# Installation features
ROOT_BUILD_FEATURE(gnuinstall OFF "Perform installation following the GNU guidelines")

#--- Removing PCH in ROOT for cxxmodules-------------------------------------------------------------
if (runtime_cxxmodules)
   set(pch_defvalue OFF)
endif(runtime_cxxmodules)

#--- Compression algorithms in ROOT-------------------------------------------------------------
if(NOT compression_default MATCHES "zlib|lz4|lzma")
   message(STATUS "Not supported compression algorithm, ROOT compression algorithms are zlib, lzma and lz4.
      ROOT will fall back to default algorithm: lz4")
   set(compression_default "lz4" CACHE STRING "" FORCE)
else()
   message(STATUS "ROOT default compression algorithm is " ${compression_default})
endif()

foreach(opt ${root_build_features})
# builtin_llvm|builtin_clang - we will use external LLVM and clang
# LLVM 5.0 is mostly delivered by OS PM, lets try toi minimize a base's build dependencies
   if(NOT opt MATCHES "cling|builtin_clang|explicitlink")
        set(${opt}_defvalue OFF)
   endif()
endforeach()

#---Define at moment the features with the selected default values-----------------------------
ROOT_APPLY_FEATURES()

#---Avoid creating dependencies to 'non-standard' header files -------------------------------
include_regular_expression("^[^.]+$|[.]h$|[.]icc$|[.]hxx$|[.]hpp$")

#---Add Installation Variables------------------------------------------------------------------
include(RootInstallDirs)

#---RPATH options-------------------------------------------------------------------------------
#  When building, don't use the install RPATH already (but later on when installing)
set(CMAKE_SKIP_BUILD_RPATH FALSE)         # don't skip the full RPATH for the build tree
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) # use always the build RPATH for the build tree
set(CMAKE_MACOSX_RPATH TRUE)              # use RPATH for MacOSX
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE) # point to directories outside the build tree to the install RPATH

# Check whether to add RPATH to the installation (the build tree always has the RPATH enabled)
if(rpath)
  set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}) # install LIBDIR
  set(CMAKE_SKIP_INSTALL_RPATH FALSE)          # don't skip the full RPATH for the install tree
elseif(APPLE)
  set(CMAKE_INSTALL_NAME_DIR "@rpath")
  if(gnuinstall)
    set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}) # install LIBDIR
  else()
    set(CMAKE_INSTALL_RPATH "@loader_path/../lib")    # self relative LIBDIR
  endif()
  set(CMAKE_SKIP_INSTALL_RPATH FALSE)          # don't skip the full RPATH for the install tree
else()
  set(CMAKE_SKIP_INSTALL_RPATH TRUE)           # skip the full RPATH for the install tree
endif()

#---deal with the DCMAKE_IGNORE_PATH------------------------------------------------------------
if(macos_native)
  if(APPLE)
    set(CMAKE_IGNORE_PATH)
    foreach(_prefix /sw /opt/local /usr/local) # Fink installs in /sw, and MacPort in /opt/local and Brew in /usr/local
      list(APPEND CMAKE_IGNORE_PATH ${_prefix}/bin ${_prefix}/include ${_prefix}/lib)
    endforeach()
  else()
    message(STATUS "Option 'macos_native' is only for MacOS systems. Ignoring it.")
  endif()
endif()
