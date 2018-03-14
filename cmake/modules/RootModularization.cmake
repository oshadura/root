#---------------------------------------------------------------------------------------------------
#  RootModularization.cmake
#---------------------------------------------------------------------------------------------------
#include(RootModuleMap)
include(RootVariables)
#---------------------------------------------------------------------------------------------------
set(root_modules)
set(root_packages)
SET_PROPERTY(GLOBAL PROPERTY root_modules "")
SET_PROPERTY(GLOBAL PROPERTY root_packages "")
#---------------------------------------------------------------------------------------------------
#---ROOT_MODULARIZATION_PACKAGE(name defvalue [description] )
#---Now is avaibale only for ALL & BASE
#---------------------------------------------------------------------------------------------------
function(ROOT_MODULARIZATION_PACKAGE build defvalue)
  if(ARGN)
    set(description ${ARGN})
  else()
    set(description " ")
  endif()
  set(${build}_defvalue    ${defvalue} PARENT_SCOPE)
  set(${build}_description ${description} PARENT_SCOPE)
  set(root_packages  ${root_packages} ${build} PARENT_SCOPE)
  if(${defvalue})
  	message(STATUS ${description})
  endif()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_APPLY_PACKAGE()
#---------------------------------------------------------------------------------------------------
function(ROOT_APPLY_PACKAGES)
  foreach(opt ${root_packages})
     option(${opt} "${${opt}_description}" ${${opt}_defvalue})
  endforeach()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_PACKAGES([var] )
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_PACKAGES)
  set(enabled)
  foreach(opt ${root_packages})
    if(${opt})
      list(APPEND enabled ${opt})
    endif()
  endforeach()
  if(NOT ARGN)
    message(STATUS "Enabled packages for ROOT: ${enabled}")
  else()
    set(${ARGN} "${enabled}" PARENT_SCOPE)
  endif()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ListToString([var] )
#---------------------------------------------------------------------------------------------------

function (ListToString result delim)
    list(GET ARGV 2 temp)
    math(EXPR N "${ARGC}-1")
    foreach(IDX RANGE 3 ${N})
        list(GET ARGV ${IDX} STR)
        set(temp "${temp}${delim}${STR}")
    endforeach()
    set(${result} "${temp}" PARENT_SCOPE)
endfunction(ListToString)

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_MODULES([var] )
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_MODULES)
  ListToString(roomodules ", " ${root_modules})
  message(STATUS "${roomodules}")
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_GET_PACKAGE([var])
#---------------------------------------------------------------------------------------------------
function(ROOT_GET_PACKAGE list_name return_value)
  foreach(opt ${root_packages})
    if(${opt} STREQUAL ${listname})
      set(return_value ON)
      message(STATUS ${return_value})
    endif()
  endforeach()
  set(${return_value} PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------------------------------
#---ROOT_PRINT_TUPLE_VALUE([var] )
#-----------------------------------------------------------------------------------------------------
function(ROOT_PRINT_TUPLE_VALUES tuplename)
  foreach(list_name IN LISTS tuplename)
    message(STATUS "For component " ${list_name} ":")
    foreach(value IN LISTS ${list_name})
      message(STATUS "-> defined next library: " ${value})
    endforeach()
  endforeach()
endfunction()

#-----------------------------------------------------------------------------------------------------
#---ROOT_GET_LIST_VALUE(tuplename package position)
#-----------------------------------------------------------------------------------------------------
function(ROOT_GET_LIST_NAME rootpackagemap module list_name)
  set(list_name_output ${list_name})
  foreach(listname IN LISTS rootpackagemap)
    foreach(value IN LISTS ${listname})
      #message(STATUS ${listname} " and " ${value})
      message(STATUS ${module} " = " ${value})
      if(${value} STREQUAL ${module})
        message(STATUS "We grepped next list " ${listname} " for the module " ${module})
        set(list_name_output ${listname})
      endif()
    endforeach()
  endforeach()
 set(${list_name} ${list_name_output} PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------------------------------
#---ROOT_CHECK_TUPLE_VALUE(tuplename name_of_module return_package)
#-----------------------------------------------------------------------------------------------------
function(ROOT_PARSE_TUPLE_VALUES list_name name_of_module return_package)
  set(return_package_output ${return_package})
    foreach(value IN LISTS list_name)
      message(STATUS ${value} " is not " ${name_of_module} "?")
      if(${value} STREQUAL ${name_of_module})
        set(return_package_name ${value})
        message(STATUS "We got parced " ${value} " for package " ${return_package_output})
     endif()
  endforeach()
 set(${return_package} ${return_package_output} PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------------------------------
# Generator of yaml files for ROOT_STANDARD_LIBRARY_PACKAGE(X DEPENDENCIES x)
#-----------------------------------------------------------------------------------------------------
#-----------------------------------------------------------------------------------------------------
#FIXME: to add better option's management
ROOT_MODULARIZATION_PACKAGE(BASE ON "Building ROOT Base module")
if(root-full-build)
  ROOT_MODULARIZATION_PACKAGE(ALL ON "Building ROOT Base module")
endif()
