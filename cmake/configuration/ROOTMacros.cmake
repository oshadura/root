#----------------------------------------------------------------------------
# canonicalize_tool_name(name output)
#----------------------------------------------------------------------------
#
function(canonicalize_dir_name name output)
   string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" nameStrip ${name})
   string(REPLACE "-" "_" nameUNDERSCORE ${nameStrip})
   string(TOLOWER ${nameUNDERSCORE} nameLOWER)
   set(${output} "${nameLOWER}" PARENT_SCOPE)
endfunction(canonicalize_dir_name)

#----------------------------------------------------------------------------
# add_root_subdirectory(name type)
#----------------------------------------------------------------------------
# Custom add_subdirectory wrapper.
# Takes in the subdirectory name.
#
function(add_subdirectory name)
   # Make sure that we have a generic lower-case name of the module as an input
   canonicalize_dir_name(${name} nameLOWER)
   if(IS_ABSOLUTE ${name})
      if(EXISTS "${name}")
         _add_subdirectory(${name} ${ARGN})
      else()
         message(WARNING "Missing directory in add_subdirectory(${name})")
      endif()
   else()
      if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/CMakeLists.txt)
         _add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name} ${ARGN})
      else()
         message(WARNING "Missing directory in add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${name})")
      endif()
   endif()
endfunction()
