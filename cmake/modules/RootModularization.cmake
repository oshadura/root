#---------------------------------------------------------------------------------------------------
#  RootModularization.cmake
#---------------------------------------------------------------------------------------------------
include(RootPackageMap)
#---------------------------------------------------------------------------------------------------
set(root_modules)
set(root_packages)
SET_PROPERTY(GLOBAL PROPERTY root_modules "")
SET_PROPERTY(GLOBAL PROPERTY root_packages "")
#---------------------------------------------------------------------------------------------------
#---ADD_ROOT_PACKAGE_BUILD_OPTION(name defvalue [description] )
#---------------------------------------------------------------------------------------------------
function(ADD_ROOT_PACKAGE_BUILD_OPTION package value description)
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
#---ENABLE_ROOT_PACKAGE_BUILD_OPTION(package)
#---------------------------------------------------------------------------------------------------
function(ENABLE_ROOT_PACKAGE_BUILD_OPTION ${package})
   set(${package} ON CACHE BOOL "" FORCE)
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
#---LIST_TO_STRING(result delim)
#---------------------------------------------------------------------------------------------------
function(LIST_TO_STRING result delim)
    list(GET ARGV 2 temp)
    math(EXPR N "${ARGC}-1")
    foreach(IDX RANGE 3 ${N})
        list(GET ARGV ${IDX} STR)
        set(temp "${temp}${delim}${STR}")
    endforeach()
    set(${result} "${temp}" PARENT_SCOPE)
endfunction(LIST_TO_STRING)

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_PACKAGES()
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_PACKAGES)
  LIST_TO_STRING(rpackages ", " ${root_packages})
  message(STATUS "${rpackages}")
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_MODULES()
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_MODULES)
  LIST_TO_STRING(rmodules ", " ${root_modules})
  message(STATUS "${rmodules}")
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

#---------------------------------------------------------------------------------------------------
#---ENABLE_PACKAGES_FROM_MAP()
#---------------------------------------------------------------------------------------------------
function(ENABLE_PACKAGES_FROM_MAP)
   foreach(value IN LISTS rootpackagemap_requested)
      if(ARGN)
         set(description ${ARGN})
      else()
         set(description " ")
      endif()
      set(${value}_defvalue ON CACHE BOOL "" FORCE PARENT_SCOPE)
      set(${value}_description ${description} PARENT_SCOPE)
      list(APPEND root_packages ${value})
      set(root_packages ${root_packages} PARENT_SCOPE)
      if(${defvalue})
         message(STATUS ${description})
      endif()
      option(${value} "" ${${value}_defvalue}  PARENT_SCOPE)
   endforeach()
endfunction()
