#include(RootNewMacros)

include(CheckCCompilerFlag)

find_package(ROOT REQUIRED COMPONENTS RIO)
include(${ROOT_USE_FILE})

get_directory_property(RootFramework_incdirs INCLUDE_DIRECTORIES)
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "${RootFramework_incdirs};${CMAKE_CURRENT_SOURCE_DIR}/inc;${ROOT_INCLUDE_DIR}")

#----------------------------------------------------------------------------
# ROOT_ADD_CXX_FLAG(var flag)
#----------------------------------------------------------------------------
function(ROOT_ADD_CXX_FLAG var flag)
  string(REGEX REPLACE "[-.+/:= ]" "_" flag_esc "${flag}")
  CHECK_CXX_COMPILER_FLAG("-Werror ${flag}" CXX_HAS${flag_esc})
  if(CXX_HAS${flag_esc})
    set(${var} "${${var}} ${flag}" PARENT_SCOPE)
  endif()
endfunction()
#----------------------------------------------------------------------------
# ROOT_ADD_C_FLAG(var flag)
#----------------------------------------------------------------------------
function(ROOT_ADD_C_FLAG var flag)
  string(REGEX REPLACE "[-.+/:= ]" "_" flag_esc "${flag}")
  CHECK_C_COMPILER_FLAG("-Werror ${flag}" C_HAS${flag_esc})
  if(C_HAS${flag_esc})
    set(${var} "${${var}} ${flag}" PARENT_SCOPE)
  endif()
endfunction()

function(ROOT_ADD_TEST_SUBDIRECTORY dummy)
endfunction()

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

  set(NEW_SOURCES)
  foreach(fp ${ARG_SOURCES})
    if(${fp} MATCHES "[*?]") # Is this header a globbing expression?
      file(GLOB_RECURSE files "src/${fp}")
      list(APPEND NEW_SOURCES "${files}")
    else()
      list(APPEND NEW_SOURCES "${fp}")
    endif()
  endforeach()
  set(ARG_SOURCES "${NEW_SOURCES}")

  list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

  list(APPEND dlist "")
  list(APPEND srclist "")
  list(APPEND hdrlist "")

  foreach(f ${ARG_DEPENDENCIES})
    list(APPEND dlist ${f})
  endforeach()

  if(ARG_SOURCES)
    foreach(f ${ARG_SOURCES})
      list(APPEND srclist ${f})
    endforeach()
  elseif(ARG_NO_SOURCES)
     list(APPEND srclist "no_sources")
  else()
     list(APPEND srclist "src/*.cxx")
  endif()

  if(ARG_HEADERS)
    foreach(f ${ARG_HEADERS})
      list(APPEND hdrlist ${f})
    endforeach()
  elseif(ARG_NO_HEADERS)
    list(APPEND hdrlist "no_headers")
  else()
    list(APPEND hdrlist "*.h")
  endif()

  message(STATUS "Dependencies: " ${dlist})
  message(STATUS "Sources: " ${srclist})
  message(STATUS "Headers: " ${hdrlist})

  # We consider that we always have root-modularization ON (building outside of ROOT sources)
  generate_module_manifest(${libname} ${dlist} ${srclist} ${hdrlist})

  list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/inc" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

  set(dep_include_dirs)
  set(PKG_PATH "$ENV{ROOT_PKG_PATH}")
  list(REMOVE_ITEM ARG_DEPENDENCIES Core RIO)
  foreach(DEP ${ARG_DEPENDENCIES})
    include("${PKG_PATH}/${DEP}/${DEP}.cmake")
    get_property(new_dep_include_dirs TARGET ${DEP}_${DEP} PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND dep_include_dirs "${new_dep_include_dirs}")
    get_directory_property(RootFramework_incdirs INCLUDE_DIRECTORIES)
    set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "${RootFramework_incdirs};${new_dep_include_dirs}")
  endforeach()


  ROOT_GENERATE_DICTIONARY(${libname} ${ARG_HEADERS}
#                          MODULE ${libname}
                           LINKDEF ${ARG_LINKDEF}
                           OPTIONS ${ARG_DICTIONARY_OPTIONS}
                           )

  add_library(${libname} SHARED ${ARG_SOURCES} ${libname}.cxx)
  target_link_libraries(${libname} Core ${ARG_LIBRARIES})

  foreach(DEP ${ARG_DEPENDENCIES})
    target_link_libraries(${libname} "${PKG_PATH}/${DEP}/lib${DEP}.so")
  endforeach()


  file(WRITE dependencies "${ARG_DEPENDENCIES}")

  set(INSTALL_DIR "${CMAKE_INSTALL_PREFIX}")

  install(TARGETS ${libname} EXPORT ${libname} LIBRARY DESTINATION "${INSTALL_DIR}"
          INCLUDES DESTINATION "${INSTALL_DIR}/inc" "${dep_include_dirs}")
  install(EXPORT ${libname} NAMESPACE ${libname}_ DESTINATION "${INSTALL_DIR}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${libname}.rootmap" DESTINATION "${INSTALL_DIR}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${libname}_rdict.pcm" DESTINATION "${INSTALL_DIR}")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dependencies" DESTINATION "${INSTALL_DIR}")
  install(DIRECTORY inc DESTINATION "${INSTALL_DIR}")
endfunction()

macro(generate_module_manifest libname dlist srclist hdrlist)
  list(APPEND dlist_pretty_print "")
  list(APPEND srclist_pretty_print "")
  list(APPEND hdrlist_pretty_print "")

  foreach(f ${dlist})
    list(APPEND dlist_pretty_print ${f} )
    #list(APPEND dlist_pretty_print " ")
  endforeach()

  foreach(f ${srclist})
    list(APPEND srclist_pretty_print ${f} )
  #  list(APPEND srclist_pretty_print " ")
  endforeach()

  foreach(f ${hdrlist})
    list(APPEND srclist_pretty_print ${f})
    #list(APPEND srclist_pretty_print " ")
  endforeach()
  string(TOLOWER ${libname} pkgname)
  file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "module: \n  name:  " ${libname}\n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  packageurl: " \"https://github.com/root-project/${pkgname}\"\n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  tag: 0.0.0 " \n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  path: " ${CMAKE_CURRENT_SOURCE_DIR}\n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  publicheaders: " ${hdrlist_pretty_print}\n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  sources: ${srclist_pretty_print}" \n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  targets: " ${libname}\n)
  file(APPEND "${CMAKE_CURRENT_SOURCE_DIR}/module.yml" "  deps: " ${dlist_pretty_print}\n)

  get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
  message(STATUS "Printing yml configuration for dir " ${PARENT_DIR})

  get_filename_component(PARENT_PATH ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
  message(STATUS "Printing yml configuration for path " ${PARENT_PATH})

  if("${PARENT_DIR}" STREQUAL "${listname}")
    generate_package_manifest(${listname})
    file(APPEND "${PARENT_PATH}/package.yml" "module: \n  name:  " ${libname}\n)
    file(APPEND "${PARENT_PATH}/package.yml" "packageurl: " \"https://github.com/root-project/${pkgname}\"\n)
    file(APPEND "${PARENT_PATH}/package.yml" "tag: 0.0.0" \n)
    file(APPEND "${PARENT_PATH}/package.yml" "path: " ${CMAKE_CURRENT_SOURCE_DIR}\n)
    file(APPEND "${PARENT_PATH}/package.yml" "publicheaders: " ${hdrlist_pretty_print}\n)
    file(APPEND "${PARENT_PATH}/package.yml" "sources: ${srclist_pretty_print}" \n)
    file(APPEND "${PARENT_PATH}/package.yml" "targets: " ${libname_pretty_print}\n)
    file(APPEND "${PARENT_PATH}/package.yml" "deps: " ${dlist_pretty_print}}\n)
  endif()
endmacro()
