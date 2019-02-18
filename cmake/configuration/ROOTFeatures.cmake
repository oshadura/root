set(root_build_options)

#---------------------------------------------------------------------------------------------------
#---ROOT_BUILD_OPTION( name defvalue [description] )
#---------------------------------------------------------------------------------------------------
function(ROOT_BUILD_OPTION opt defvalue)
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
#---ROOT_APPLY_OPTIONS()
#---------------------------------------------------------------------------------------------------
function(ROOT_APPLY_OPTIONS)
  foreach(opt ${root_build_options})
     option(${opt} "${${opt}_description}" ${${opt}_defvalue})
  endforeach()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_OPTIONS([var] )
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_OPTIONS)
  set(enabled)
  foreach(opt ${root_build_options})
    if(${opt})
      set(enabled "${enabled} ${opt}")
    endif()
  endforeach()
  if(NOT ARGN)
    message(STATUS "Enabled support for: ${enabled}")
  else()
    set(${ARGN} "${enabled}" PARENT_SCOPE)
  endif()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_GET_OPTIONS(result ENABLED)
#---------------------------------------------------------------------------------------------------
function(ROOT_GET_OPTIONS result)
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
#---ROOT_WRITE_OPTIONS(file )
#---------------------------------------------------------------------------------------------------
function(ROOT_WRITE_OPTIONS file)
  file(WRITE ${file} "#---Options enabled for the build of ROOT-----------------------------------------------\n")
  foreach(opt ${root_build_options})
    if(${opt})
      file(APPEND ${file} "set(${opt} ON)\n")
    else()
      file(APPEND ${file} "set(${opt} OFF)\n")
    endif()
  endforeach()
endfunction()

###################################################################################################
######################### ROOT Generic Options ####################################################
###################################################################################################
# ROOT options that doesnt activate/disactivate packages/modules but qualify a build type
set(compression_default "lz4" CACHE STRING "ROOT compression algorithm used as a default, default option is lz4. Can be lz4, zlib, or lzma")
set(gcctoolchain "" CACHE PATH "Path for the gcctoolchain in case not the system gcc is used to build clang/LLVM")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
   ROOT_BUILD_OPTION(ccache ON "Enable ccache usage for speeding up builds")
else()
   ROOT_BUILD_OPTION(ccache OFF "Enable ccache usage for speeding up builds")
endif()
# C++ standarts options
ROOT_BUILD_OPTION(cxx11 ON "Build using C++11 compatible mode, requires gcc > 4.7.x or clang")
ROOT_BUILD_OPTION(cxx14 OFF "Build using C++14 compatible mode, requires gcc > 4.9.x or clang")
ROOT_BUILD_OPTION(cxx17 OFF "Build using C++17 compatible mode, requires gcc >= 7.2.0 or clang")
# Interpreter options
ROOT_BUILD_OPTION(cling ON "Enable CLING C++ interpreter")
# C++ modules options
ROOT_BUILD_OPTION(cxxmodules OFF "Compile with C++ modules enabled.")
ROOT_BUILD_OPTION(runtime_cxxmodules OFF "Enable runtime support for C++ modules.")
# ROOT pch options
ROOT_BUILD_OPTION(pch ON "Generation of PCH for ROOT")
# ROOT build options
ROOT_BUILD_OPTION(exceptions ON "Turn on compiler exception handling capability")
ROOT_BUILD_OPTION(explicitlink ON "Explicitly link with all dependent libraries")
ROOT_BUILD_OPTION(rpath OFF "Set run-time library load path on executables and shared libraries (at installation area)")
ROOT_BUILD_OPTION(shared ON "Use shared 3rd party libraries if possible")
# ROOT concurency options
ROOT_BUILD_OPTION(imt ON "Implicit multi-threading support")
ROOT_BUILD_OPTION(thread ON "Using thread library (cannot be disabled)")
# Coverage options
ROOT_BUILD_OPTION(coverage OFF "Test coverage")
# Installation options
ROOT_BUILD_OPTION(gnuinstall OFF "Perform installation following the GNU guidelines")

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

###################################################################################################
############################### Modularization ####################################################
###################################################################################################
option(root-lazy-build "ROOT modularization" ON)
#---Apply root-lazy-build (RootModularization.cmake)----------------------------------------------------------------------------
# We are removing all option that we had and building ROOT from base
# FIXME  we will remove options for builtin_llvm|builtin_clang soon, since they could be external packages

if(root-lazy-build)
   foreach(opt ${root_build_options})
   # builtin_llvm|builtin_clang - we will use external LLVM and clang
   # LLVM 5.0 is mostly delivered by OS PM, lets try toi minimize a base's build dependencies
      if(NOT opt MATCHES "cling|builtin_clang|explicitlink")
         set(${opt}_defvalue OFF)
      endif()
   endforeach()

   option(root-get "Package manager for ROOT - root-get" ON)
endif()

###################################################################################################
############################ Old style build #######################################################
###################################################################################################
# classical ROOT approach with different set of options
if(NOT root-lazy-build)
#--------------------------------------------------------------------------------------------------
#---Full list of options with their descriptios and default values
#   The default value can be changed as many times as we wish before calling ROOT_APPLY_OPTIONS()
#--------------------------------------------------------------------------------------------------
   ROOT_BUILD_OPTION(afdsmgrd OFF "Dataset manager for PROOF-based analysis facilities")
   ROOT_BUILD_OPTION(afs OFF "AFS support, requires AFS libs and objects")
   ROOT_BUILD_OPTION(alien OFF "AliEn support, requires libgapiUI from ALICE")
   ROOT_BUILD_OPTION(asimage ON "Image processing support, requires libAfterImage")
   ROOT_BUILD_OPTION(arrow OFF "Apache Arrow in memory columnar storage support")
   ROOT_BUILD_OPTION(astiff ON "Include tiff support in image processing")
   ROOT_BUILD_OPTION(bonjour OFF "Bonjour support, requires libdns_sd and/or Avahi")
# Builtins need to be translated to dependency packages
   ROOT_BUILD_OPTION(builtin_afterimage ON "Build included libAfterImage, or use system libAfterImage")
   ROOT_BUILD_OPTION(builtin_cfitsio OFF "Build the FITSIO library internally (downloading tarfile from the Web)")
   ROOT_BUILD_OPTION(builtin_davix OFF "Build the Davix library internally (downloading tarfile from the Web)")
   ROOT_BUILD_OPTION(builtin_fftw3 OFF "Build the FFTW3 library internally (downloading tarfile from the Web)")
   ROOT_BUILD_OPTION(builtin_freetype OFF "Build included libfreetype, or use system libfreetype")
   ROOT_BUILD_OPTION(builtin_ftgl ON "Build included libFTGL, or use system libftgl")
   ROOT_BUILD_OPTION(builtin_gl2ps OFF "Build included libgl2ps, or use system libgl2ps")
   ROOT_BUILD_OPTION(builtin_glew ON "Build included libGLEW, or use system libGLEW")
   ROOT_BUILD_OPTION(builtin_gsl OFF "Build the GSL library internally (downloading tarfile from the Web)")
   ROOT_BUILD_OPTION(builtin_llvm ON "Build llvm internally")
   ROOT_BUILD_OPTION(builtin_clang ON "Build clang internally")
   ROOT_BUILD_OPTION(builtin_lzma OFF "Build included liblzma, or use system liblzma")
   ROOT_BUILD_OPTION(builtin_lz4 OFF "Built included liblz4, or use system liblz4")
   ROOT_BUILD_OPTION(builtin_openssl OFF "Build OpenSSL internally, or use system OpenSSL")
   ROOT_BUILD_OPTION(builtin_pcre OFF "Build included libpcre, or use system libpcre")
   ROOT_BUILD_OPTION(builtin_tbb OFF "Build the TBB internally")
   ROOT_BUILD_OPTION(builtin_unuran OFF "Build included libunuran, or use system libunuran")
   ROOT_BUILD_OPTION(builtin_vc OFF "Build the Vc package internally")
   ROOT_BUILD_OPTION(builtin_vdt OFF "Build the VDT package internally")
   ROOT_BUILD_OPTION(builtin_veccore OFF "Build VecCore internally")
   ROOT_BUILD_OPTION(builtin_xrootd OFF "Build the XROOTD internally (downloading tarfile from the Web)")
   ROOT_BUILD_OPTION(builtin_xxhash OFF "Build included xxHash library")
   ROOT_BUILD_OPTION(builtin_zlib OFF "Build included libz, or use system libz")
   ROOT_BUILD_OPTION(castor ON "CASTOR support, requires libshift from CASTOR >= 1.5.2")
   ROOT_BUILD_OPTION(cefweb OFF "Chromium Embedded Framework web-based display")
   ROOT_BUILD_OPTION(chirp OFF "Chirp support (Condor remote I/O), requires libchirp_client")
   ROOT_BUILD_OPTION(cocoa OFF "Use native Cocoa/Quartz graphics backend (MacOS X only)")
   ROOT_BUILD_OPTION(cuda OFF "Use CUDA if it is found in the system")
   ROOT_BUILD_OPTION(davix ON "DavIx library for HTTP/WEBDAV access")
   ROOT_BUILD_OPTION(dcache OFF "dCache support, requires libdcap from DESY")
   ROOT_BUILD_OPTION(fftw3 ON "Fast Fourier Transform support, requires libfftw3")
   ROOT_BUILD_OPTION(fitsio ON "Read images and data from FITS files, requires cfitsio")
   ROOT_BUILD_OPTION(fortran OFF "Enable the Fortran components of ROOT")
   ROOT_BUILD_OPTION(gdml ON "GDML writer and reader")
   ROOT_BUILD_OPTION(genvector ON "Build the new libGenVector library")
   ROOT_BUILD_OPTION(geocad OFF "ROOT-CAD Interface")
   ROOT_BUILD_OPTION(gfal ON "GFAL support, requires libgfal")
   ROOT_BUILD_OPTION(glite OFF "gLite support, requires libglite-api-wrapper v.3 from GSI (https://subversion.gsi.de/trac/dgrid/wiki)")
   ROOT_BUILD_OPTION(globus OFF "Globus authentication support, requires Globus toolkit")
   ROOT_BUILD_OPTION(gsl_shared OFF "Enable linking against shared libraries for GSL (default no)")
   ROOT_BUILD_OPTION(gviz OFF "Graphs visualization support, requires graphviz")
   ROOT_BUILD_OPTION(hdfs OFF "HDFS support; requires libhdfs from HDFS >= 0.19.1")
   ROOT_BUILD_OPTION(http ON "HTTP Server support")
   ROOT_BUILD_OPTION(jemalloc OFF "Using the jemalloc allocator")
   ROOT_BUILD_OPTION(krb5 OFF "Kerberos5 support, requires Kerberos libs")
   ROOT_BUILD_OPTION(ldap OFF "LDAP support, requires (Open)LDAP libs")
   ROOT_BUILD_OPTION(libcxx OFF "Build using libc++, requires cxx11 option (MacOS X only, for the time being)")
   ROOT_BUILD_OPTION(macos_native OFF "Disable looking for libraries, includes and binaries in locations other than a native installation (MacOS only)")
   ROOT_BUILD_OPTION(mathmore ON "Build the new libMathMore extended math library, requires GSL (vers. >= 1.8)")
   ROOT_BUILD_OPTION(memory_termination OFF "Free internal ROOT memory before process termination (experimental, used for leak checking)")
   ROOT_BUILD_OPTION(memstat OFF "A memory statistics utility, helps to detect memory leaks")
   ROOT_BUILD_OPTION(minuit2 OFF "Build the new libMinuit2 minimizer library")
   ROOT_BUILD_OPTION(monalisa OFF "Monalisa monitoring support, requires libapmoncpp")
   ROOT_BUILD_OPTION(mysql ON "MySQL support, requires libmysqlclient")
   ROOT_BUILD_OPTION(odbc OFF "ODBC support, requires libiodbc or libodbc")
   ROOT_BUILD_OPTION(opengl ON "OpenGL support, requires libGL and libGLU")
   ROOT_BUILD_OPTION(oracle ON "Oracle support, requires libocci")
   ROOT_BUILD_OPTION(pgsql ON "PostgreSQL support, requires libpq")
   ROOT_BUILD_OPTION(pythia6 ON "Pythia6 EG support, requires libPythia6")
   ROOT_BUILD_OPTION(pythia6_nolink OFF "Delayed linking of Pythia6 library")
   ROOT_BUILD_OPTION(pythia8 ON "Pythia8 EG support, requires libPythia8")
   ROOT_BUILD_OPTION(python ON "Python ROOT bindings, requires python >= 2.2")
   ROOT_BUILD_OPTION(qt OFF "Qt4 graphics backend, requires Qt4 >= 4.8")
   ROOT_BUILD_OPTION(qtgsi OFF "GSI's Qt4 integration, requires Qt4 >= 4.8")
   ROOT_BUILD_OPTION(qt5web OFF "Qt5 web-based display, requires Qt5")
   ROOT_BUILD_OPTION(r OFF "R ROOT bindings, requires R, Rcpp and RInside")
   ROOT_BUILD_OPTION(rfio OFF "RFIO support, requires libshift from CASTOR >= 1.5.2")
   ROOT_BUILD_OPTION(roofit ON "Build the libRooFit advanced fitting package")
   ROOT_BUILD_OPTION(root7 OFF "Build the ROOT 7 interface prototype, requires >= cxx14")
   ROOT_BUILD_OPTION(rpath OFF "Set run-time library load path on executables and shared libraries (at installation area)")
   ROOT_BUILD_OPTION(ruby OFF "Ruby ROOT bindings, requires ruby >= 1.8")
   ROOT_BUILD_OPTION(runtime_cxxmodules OFF "Enable runtime support for C++ modules.")
   ROOT_BUILD_OPTION(sapdb OFF "MaxDB/SapDB support, requires libsqlod and libsqlrte")
   ROOT_BUILD_OPTION(shadowpw OFF "Shadow password support")
   ROOT_BUILD_OPTION(shared ON "Use shared 3rd party libraries if possible")
   ROOT_BUILD_OPTION(soversion OFF "Set version number in sonames (recommended)")
   ROOT_BUILD_OPTION(sqlite ON "SQLite support, requires libsqlite3")
   ROOT_BUILD_OPTION(srp OFF "SRP support, requires SRP source tree")
   ROOT_BUILD_OPTION(ssl ON "SSL encryption support, requires openssl")
   ROOT_BUILD_OPTION(table OFF "Build libTable contrib library")
   ROOT_BUILD_OPTION(tcmalloc OFF "Using the tcmalloc allocator")
   ROOT_BUILD_OPTION(tmva ON "Build TMVA multi variate analysis library")
   ROOT_BUILD_OPTION(tmva-cpu ON "Build TMVA with CPU support for deep learning. Requires BLAS")
   ROOT_BUILD_OPTION(tmva-gpu ON "Build TMVA with GPU support for deep learning. Requries CUDA")
   ROOT_BUILD_OPTION(unuran OFF "UNURAN - package for generating non-uniform random numbers")
   ROOT_BUILD_OPTION(vc OFF "Vc adds a few new types for portable and intuitive SIMD programming")
   ROOT_BUILD_OPTION(vdt OFF "VDT adds a set of fast and vectorisable mathematical functions")
   ROOT_BUILD_OPTION(veccore OFF "VecCore SIMD abstraction library")
   ROOT_BUILD_OPTION(vecgeom OFF "VecGeom is a vectorized geometry library enhancing the performance of geometry navigation.")
   ROOT_BUILD_OPTION(winrtdebug OFF "Link against the Windows debug runtime library")
   ROOT_BUILD_OPTION(x11 ON "X11 support")
   ROOT_BUILD_OPTION(xft ON "Xft support (X11 antialiased fonts)")
   ROOT_BUILD_OPTION(xml ON "XML parser interface")
   ROOT_BUILD_OPTION(xrootd ON "Build xrootd file server and its client (if supported)")

# ROOT "builds" options
   option(fail-on-missing "Fail the configure step if a required external package is missing" OFF)
   option(minimal "Do not automatically search for support libraries" OFF)
   option(gminimal "Do not automatically search for support libraries, but include X11" OFF)
   option(all "Enable all optional components" OFF)
   option(testing "Enable testing with CTest" OFF)
# ROOT external projects (tests, benchmarks and etc.)
   option(roottest "Include roottest, if roottest exists in root or if it is a sibling directory." OFF)
   option(rootbench "Include rootbench, if rootbench exists in root or if it is a sibling directory." OFF)
   option(clingtest "Include cling tests. NOTE that this makes llvm/clang symbols visible in libCling." OFF)

   #--- Minor chnages in defaults due to platform--------------------------------------------------
   if(WIN32)
     set(x11_defvalue OFF)
     set(memstat_defvalue OFF)
     set(davix_defvalue OFF)
     set(cxx11_defvalue OFF)
     set(cxx14_defvalue ON)
     set(root7_defvalue OFF)
     set(imt_defvalue OFF)
     set(builtin_tbb_defvalue OFF)
     set(tmva_defvalue OFF)
     set(roofit_defvalue OFF)
     set(roottest_defvalue OFF)
     set(testing_defvalue OFF)
   elseif(APPLE)
     set(x11_defvalue OFF)
     set(cocoa_defvalue ON)
   endif()

   #--- The 'all' option swithes ON major options---------------------------------------------------
   if(all)
    set(arrow_defvalue ON)
    set(bonjour_defvalue ON)
    set(chirp_defvalue ON)
    set(dcache_defvalue ON)
    set(fitsio_defvalue ON)
    set(glite_defvalue ON)
    set(fortran_defvalue ON)
    set(gdml_defvalue ON)
    set(gviz_defvalue ON)
    set(hdfs_defvalue ON)
    set(http_defvalue ON)
    set(krb5_defvalue ON)
    set(ldap_defvalue ON)
    set(memstat_defvalue ON)
    set(minuit2_defvalue ON)
    set(monalisa_defvalue ON)
    set(odbc_defvalue ON)
    set(qt_defvalue ON)
    set(qtgsi_defvalue ON)
    set(r_defvalue ON)
    set(rfio_defvalue ON)
    set(roofit_defvalue ON)
    set(root7_defvalue ON)
    set(sapdb_defvalue ON)
    set(shadowpw_defvalue ON)
    set(srp_defvalue ON)
    set(table_defvalue ON)
    set(unuran_defvalue ON)
    set(vc_defvalue ON)
    set(vdt_defvalue ON)
    set(veccore_defvalue ON)
   endif()

   #--- The 'builtin_all' option swithes ON old the built in options-------------------------------
   if(builtin_all)
     set(builtin_afterimage_defvalue ON)
     set(builtin_cfitsio_defvalue ON)
     set(builtin_clang_defvalue ON)
     set(builtin_davix_defvalue ON)
     set(builtin_fftw3_defvalue ON)
     set(builtin_freetype_defvalue ON)
     set(builtin_ftgl_defvalue ON)
     set(builtin_gl2ps_defvalue ON)
     set(builtin_glew_defvalue ON)
     set(builtin_gsl_defvalue ON)
     set(builtin_llvm_defvalue ON)
     set(builtin_lz4_defvalue ON)
     set(builtin_lzma_defvalue ON)
     set(builtin_openssl_defvalue ON)
     set(builtin_pcre_defvalue ON)
     set(builtin_tbb_defvalue ON)
     set(builtin_unuran_defvalue ON)
     set(builtin_vc_defvalue ON)
     set(builtin_vdt_defvalue ON)
     set(builtin_veccore_defvalue ON)
     set(builtin_xrootd_defvalue ON)
     set(builtin_xxhash_defvalue ON)
     set(builtin_zlib_defvalue ON)
   endif()

   #---Apply root7 versus language------------------------------------------------------------------
   if(cxx14 OR cxx17 OR cxx14_defval OR cxx17_defval)
     set(root7_defvalue ON)
   endif()

   #---Vc supports only x86_64 architecture-------------------------------------------------------
   if (NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
     message(STATUS "Vc does not support ${CMAKE_SYSTEM_PROCESSOR}. Support for Vc disabled.")
     set(vc_defvalue OFF)
   endif()

   #---Options depending of CMake Generator-------------------------------------------------------
   if( CMAKE_GENERATOR STREQUAL Ninja)
      set(fortran_defvalue OFF)
   endif()

   #---Apply minimal or gminimal------------------------------------------------------------------
   foreach(opt ${root_build_options})
     if(NOT opt MATCHES "thread|cxx11|cling|builtin_llvm|builtin_clang|builtin_ftgl|explicitlink")
       if(minimal)
         set(${opt}_defvalue OFF)
       elseif(gminimal AND NOT opt MATCHES "x11|cocoa")
         set(${opt}_defvalue OFF)
       endif()
     endif()
   endforeach()

   #---roottest option implies testing
   if(roottest OR rootbench)
     set(testing ON CACHE BOOL "" FORCE)
   endif()
endif()
###################################################################################################
###################################################################################################
###################################################################################################
#---Define at moment the options with the selected default values-----------------------------
ROOT_APPLY_OPTIONS()
if(root-lazy-build)
   ENABLE_PACKAGES_FROM_MAP()
endif()

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
