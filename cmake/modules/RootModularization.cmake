#---------------------------------------------------------------------------------------------------
#  RootModularization.cmake
#---------------------------------------------------------------------------------------------------
set(root_build)
set(root_modules)
SET_PROPERTY(GLOBAL PROPERTY root_modules "")
#---------------------------------------------------------------------------------------------------
#---ROOT_MODULARIZATION_OPTION( name defvalue [description] )
#---------------------------------------------------------------------------------------------------

function(ROOT_MODULARIZATION_OPTION build defvalue)
  if(ARGN)
    set(description ${ARGN})
  else()
    set(description " ")
  endif()
  set(${build}_defvalue    ${defvalue} PARENT_SCOPE)
  set(${build}_description ${description} PARENT_SCOPE)
  set(root_build  ${root_build} ${build} PARENT_SCOPE)
  if(${defvalue})
  	message(STATUS ${description})
  endif()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_APPLY_OPTIONS()
#---------------------------------------------------------------------------------------------------
function(ROOT_APPLY_BUILD)
  foreach(opt ${root_build})
     option(${opt} "${${opt}_description}" ${${opt}_defvalue})
  endforeach()  
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_OPTIONS([var] )
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_BUILD)
  set(enabled)
  foreach(opt ${root_build})
    if(${opt})
      set(enabled "${enabled} ${opt}")
    endif()
  endforeach()
  if(NOT ARGN)
    message(STATUS "Enabled build for ROOT: ${enabled}")
  else()
    set(${ARGN} "${enabled}" PARENT_SCOPE)
  endif()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_MODULES([var] )
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_MODULES)
  set(enabled)
  foreach(opt ${root_modules})
    if(${opt})
      set(enabled "${enabled} ${opt}")
    endif()
  endforeach()
  if(NOT ARGN)
    message(STATUS "Enabled modules for ROOT: ${enabled}")
  else()
    set(${ARGN} "${enabled}" PARENT_SCOPE)
  endif()
endfunction()

#-----------------------------------------------------------------------------------------------------
#FIXME: to add better option's management
ROOT_MODULARIZATION_OPTION(base_build ON "Building ROOT Base module")
ROOT_MODULARIZATION_OPTION(advanced_build OFF "Building ROOT advanced build")
#-----------------------------------------------------------------------------------------------------
ROOT_APPLY_BUILD()
#-----------------------------------------------------------------------------------------------------
ROOT_SHOW_BUILD()