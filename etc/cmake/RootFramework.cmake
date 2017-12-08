#include(RootNewMacros)

find_package(ROOT REQUIRED COMPONENTS RIO)
include(${ROOT_USE_FILE})

get_directory_property(RootFramework_incdirs INCLUDE_DIRECTORIES)
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "${RootFramework_incdirs};${CMAKE_CURRENT_SOURCE_DIR}/inc;${ROOT_INCLUDE_DIR}")

#---------------------------------------------------------------------------------------------------
#---ROOT_STANDARD_LIBRARY_PACKAGE(libname
#                                 [NO_INSTALL_HEADERS]         : don't install headers for this package
#                                 [STAGE1]                     : use rootcling_stage1 for generating
#                                 HEADERS header1 header2      : if not specified, globbing for *.h is used)
#                                 [NO_HEADERS]                 : don't glob to fill HEADERS variable
#                                 SOURCES source1 source2      : if not specified, globbing for *.cxx is used)
#                                 [NO_SOURCES]                 : don't glob to fill SOURCES variable
#                                 [OBJECT_LIBRARY]             : use ROOT_OBJECT_LIBRARY to generate object files
#                                                                and then use those for linking.
#                                 LIBRARIES lib1 lib2          : linking flags such as dl, readline
#                                 DEPENDENCIES lib1 lib2       : dependencies such as Core, MathCore
#                                 BUILTINS builtin1 builtin2   : builtins like AFTERIMAGE
#                                 LINKDEF LinkDef.h LinkDef2.h : linkdef files, default value is "LinkDef.h"
#                                 DICTIONARY_OPTIONS option    : options passed to rootcling
#                                 INSTALL_OPTIONS option       : options passed to install headers
#                                )
#---------------------------------------------------------------------------------------------------
function(ROOT_STANDARD_LIBRARY_PACKAGE libname)
  project(${libname})

  set(options NO_INSTALL_HEADERS STAGE1 NO_HEADERS NO_SOURCES OBJECT_LIBRARY)
  set(oneValueArgs)
  set(multiValueArgs DEPENDENCIES HEADERS SOURCES BUILTINS LIBRARIES DICTIONARY_OPTIONS LINKDEF INSTALL_OPTIONS)
  CMAKE_PARSE_ARGUMENTS(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Check if we have any unparsed arguments
  if(ARG_UNPARSED_ARGUMENTS)
    message(AUTHOR_WARNING "Unparsed arguments for ROOT_STANDARD_LIBRARY_PACKAGE: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  # Check that the user doesn't parse NO_HEADERS to disable globbing and HEADERS at the same time.
  if (ARG_HEADERS AND ARG_NO_HEADERS)
    message(AUTHOR_WARNING "HEADERS and NO_HEADERS arguments are mutually exclusive.")
  endif()
  if (ARG_SOURCES AND ARG_NO_SOURCES)
    message(AUTHOR_WARNING "SOURCES and NO_SOURCES arguments are mutually exclusive.")
  endif()

  # Set default values
  # If HEADERS/SOURCES are not parsed, we glob for those files.
  if (NOT ARG_HEADERS AND NOT ARG_NO_HEADERS)
    set(ARG_HEADERS "*.h")
  endif()
  if (NOT ARG_SOURCES AND NOT ARG_NO_SOURCES)
    file(GLOB_RECURSE ARG_SOURCES "*.cxx")
  endif()
  if (NOT ARG_LINKDEF)
    set(ARG_LINKDEF "LinkDef.h")
  endif()

  if (ARG_STAGE1)
    set(STAGE1_FLAG "STAGE1")
  endif()

  #
  list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})


  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/inc" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")



  ROOT_GENERATE_DICTIONARY(G__${libname} ${ARG_HEADERS}
#                          MODULE ${libname}
                           LINKDEF ${ARG_LINKDEF}
                           OPTIONS ${ARG_DICTIONARY_OPTIONS}
                           )

  add_library(${libname} SHARED ${ARG_SOURCES} G__${libname}.cxx)
  target_link_libraries(${libname} Core ${ARG_LIBRARIES} ${ARG_DEPENDENCIES})

endfunction()
